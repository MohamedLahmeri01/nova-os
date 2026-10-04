/* NOVA OS IPv4 + ICMP + loopback (Phase 9a).
 * Big-endian wire order throughout (shifts, never casts). Loopback
 * is synchronous memory (tx copies into the rx slot); real NICs
 * replace the tx/rx pair with descriptor rings (Phase 9b keeps this
 * API: upper layers never see the difference).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "net/net.h"
#include "syscall/syscall.h"
#include "time/time.h"

uint16_t net_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t i = 0;
    while (i + 1u < len) {
        sum += ((uint32_t)data[i] << 8) | data[i + 1u];
        if (sum > 0xFFFFu) {
            sum = (sum & 0xFFFFu) + 1u;
        }
        i += 2u;
    }
    if (i < len) {
        sum += (uint32_t)data[i] << 8;
        if (sum > 0xFFFFu) {
            sum = (sum & 0xFFFFu) + 1u;
        }
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

int net_ip_build(uint8_t *out, uint32_t src, uint32_t dst, uint8_t proto,
                 uint16_t payload_len) {
    uint32_t total;
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    total = 20u + payload_len;
    if (total > NET_MTU || total < payload_len) {
        return -NOVA_EINVAL;
    }
    out[0] = 0x45u;
    out[1] = 0x00u;
    wr16(out + 2, (uint16_t)total);
    wr16(out + 4, 0x1234u); /* identification: fixed (no frag yet) */
    wr16(out + 6, 0x4000u); /* don't fragment */
    out[8] = 64u;           /* TTL */
    out[9] = proto;
    wr16(out + 10, 0);
    wr32(out + 12, src);
    wr32(out + 16, dst);
    wr16(out + 10, net_checksum(out, 20));
    return 20;
}

int net_ip_parse(const uint8_t *pkt, uint32_t len, uint8_t *proto_out,
                 uint32_t *src_out, uint32_t *dst_out) {
    uint32_t ihl;
    uint32_t total;
    if (pkt == 0 || len < 20u) {
        return -NOVA_EINVAL;
    }
    if ((pkt[0] >> 4) != 4u) {
        return -NOVA_EINVAL;
    }
    ihl = (uint32_t)(pkt[0] & 0x0Fu) * 4u;
    if (ihl < 20u || ihl > len) {
        return -NOVA_EINVAL;
    }
    total = ((uint32_t)pkt[2] << 8) | pkt[3];
    if (total > len || total < ihl) {
        return -NOVA_EINVAL;
    }
    if (net_checksum(pkt, ihl) != 0) {
        return -NOVA_EINVAL;
    }
    if (proto_out != 0) {
        *proto_out = pkt[9];
    }
    if (src_out != 0) {
        *src_out = ((uint32_t)pkt[12] << 24) |
                   ((uint32_t)pkt[13] << 16) |
                   ((uint32_t)pkt[14] << 8) | pkt[15];
    }
    if (dst_out != 0) {
        *dst_out = ((uint32_t)pkt[16] << 24) |
                   ((uint32_t)pkt[17] << 16) |
                   ((uint32_t)pkt[18] << 8) | pkt[19];
    }
    return (int)ihl;
}

int net_icmp_build(uint8_t *out, uint16_t ident, uint16_t seq,
                   const uint8_t *payload, uint32_t payload_len) {
    uint32_t total;
    uint32_t i;
    if (out == 0 || (payload == 0 && payload_len != 0)) {
        return -NOVA_EINVAL;
    }
    total = 8u + payload_len;
    if (total < payload_len || total > NET_MTU) {
        return -NOVA_EINVAL;
    }
    out[0] = 8u; /* echo request */
    out[1] = 0;
    wr16(out + 2, 0);
    wr16(out + 4, ident);
    wr16(out + 6, seq);
    for (i = 0; i < payload_len; i++) {
        out[8u + i] = payload[i];
    }
    wr16(out + 2, net_checksum(out, total));
    return (int)total;
}

int net_icmp_check(const uint8_t *pkt, uint32_t len, uint16_t ident,
                   uint16_t seq) {
    uint16_t got_ident, got_seq;
    if (pkt == 0 || len < 8u) {
        return -NOVA_EINVAL;
    }
    if (pkt[0] != 0 || pkt[1] != 0) {
        return -NOVA_EINVAL; /* want echo reply */
    }
    if (net_checksum(pkt, len) != 0) {
        return -NOVA_EINVAL;
    }
    got_ident = (uint16_t)((uint32_t)pkt[4] << 8 | pkt[5]);
    got_seq = (uint16_t)((uint32_t)pkt[6] << 8 | pkt[7]);
    if (got_ident != ident || got_seq != seq) {
        return -NOVA_EINVAL;
    }
    return NOVA_ESUCCESS;
}

/* Loopback: single packet slot (v0.1: no queueing; a second tx
 * overwrites — callers serialize, documented). */
static uint8_t g_loop_buf[NET_MTU];
static uint32_t g_loop_len;
static int g_loop_has;
static uint32_t g_loop_tx;
static uint32_t g_loop_rx;

int net_loop_tx(const uint8_t *pkt, uint32_t len) {
    uint32_t i;
    if (pkt == 0 || len == 0 || len > NET_MTU) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < len; i++) {
        g_loop_buf[i] = pkt[i];
    }
    g_loop_len = len;
    g_loop_has = 1;
    g_loop_tx++;
    return NOVA_ESUCCESS;
}

int net_loop_rx(uint8_t *out, uint32_t max) {
    uint32_t i;
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    if (!g_loop_has) {
        return -NOVA_EAGAIN;
    }
    if (g_loop_len > max) {
        return -NOVA_ENOSPC;
    }
    for (i = 0; i < g_loop_len; i++) {
        out[i] = g_loop_buf[i];
    }
    g_loop_has = 0;
    g_loop_rx++;
    return (int)g_loop_len;
}

void net_loop_stats(uint32_t *tx_out, uint32_t *rx_out) {
    if (tx_out != 0) {
        *tx_out = g_loop_tx;
    }
    if (rx_out != 0) {
        *rx_out = g_loop_rx;
    }
}

int32_t net_ping(uint32_t addr, uint16_t seq) {
    /* 512B cap: bigger frames trip __chkstk_ms (no libgcc on link). */
    static uint8_t tx[512];
    static uint8_t rx[512];
    uint8_t payload[32];
    uint32_t t0;
    int32_t dt;
    int iplen, iclen, off;
    uint8_t proto = 0;
    uint32_t src = 0, dst = 0;
    uint32_t i;
    if (addr != NET_ADDR_LOOPBACK) {
        return -NOVA_EHOSTUNREACH;
    }
    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i & 0xFFu);
    }
    iclen = net_icmp_build(tx + 20, 0xA0u, seq, payload,
                           sizeof(payload));
    if (iclen < 0) {
        return iclen;
    }
    iplen = net_ip_build(tx, NET_ADDR_LOOPBACK, addr, NET_PROTO_ICMP,
                         (uint16_t)iclen);
    if (iplen < 0) {
        return iplen;
    }
    t0 = g_ticks;
    if (net_loop_tx(tx, (uint32_t)iplen + (uint32_t)iclen) != 0) {
        return -NOVA_EIO;
    }
    /* Loopback serve: receive our own request, answer it in place
     * (echo reply = type 0 + recomputed checksum), requeue. Real
     * NICs answer on the wire; the upper half is identical. */
    {
        int rlen = net_loop_rx(rx, sizeof(rx));
        int roff;
        uint8_t rp = 0;
        if (rlen < 0) {
            return rlen;
        }
        roff = net_ip_parse(rx, (uint32_t)rlen, &rp, &src, &dst);
        if (roff < 0 || rp != NET_PROTO_ICMP) {
            return -NOVA_EIO;
        }
        rx[roff] = 0; /* request -> reply */
        wr16(rx + roff + 2u, 0);
        wr16(rx + roff + 2u,
             net_checksum(rx + roff, (uint32_t)rlen - (uint32_t)roff));
        if (net_loop_tx(rx, (uint32_t)rlen) != 0) {
            return -NOVA_EIO;
        }
    }
    {
        int rlen = net_loop_rx(rx, sizeof(rx));
        if (rlen < 0) {
            return rlen;
        }
        off = net_ip_parse(rx, (uint32_t)rlen, &proto, &src, &dst);
        if (off < 0 || proto != NET_PROTO_ICMP ||
            dst != NET_ADDR_LOOPBACK) {
            return -NOVA_EIO;
        }
        if (net_icmp_check(rx + off, (uint32_t)rlen - (uint32_t)off,
                           0xA0u, seq) != 0) {
            return -NOVA_EIO;
        }
        /* Payload echoed intact? */
        for (i = 0; i < sizeof(payload); i++) {
            if (rx[off + 8u + i] != payload[i]) {
                return -NOVA_EIO;
            }
        }
    }
    dt = (int32_t)(g_ticks - t0);
    return (dt < 0) ? 0 : dt;
}

int net_init(void) {
    g_loop_len = 0;
    g_loop_has = 0;
    g_loop_tx = 0;
    g_loop_rx = 0;
    return NOVA_ESUCCESS;
}

int net_selftest(void) {
    /* 512B cap: bigger frames trip __chkstk_ms (no libgcc on link). */
    static uint8_t pkt[512];
    uint8_t proto = 0;
    uint32_t src = 0, dst = 0;
    int32_t rtt;
    /* RFC 1071 checksum vectors (host-oracle verified: 8B even ->
     * 0x220d, 7B odd -> 0x2304, empty -> 0xffff). */
    {
        static const uint8_t v1[8] = { 0x00, 0x01, 0xF2, 0x03,
                                       0xF4, 0xF5, 0xF6, 0xF7 };
        if (net_checksum(v1, 8) != 0x220Du) {
            serial_puts("NET-FAIL cksum-vec\n");
            return -1;
        }
        if (net_checksum(v1, 7) != 0x2304u) {
            serial_puts("NET-FAIL cksum-odd\n");
            return -1;
        }
        if (net_checksum(v1, 0) != 0xFFFFu) {
            serial_puts("NET-FAIL cksum-empty\n");
            return -1;
        }
    }
    /* IP build -> parse round-trip (header must verify). */
    {
        int hl = net_ip_build(pkt, NET_ADDR_LOOPBACK,
                              NET_ADDR_LOOPBACK, NET_PROTO_ICMP, 8);
        int off;
        if (hl != 20) {
            serial_puts("NET-FAIL ip-build\n");
            return -1;
        }
        off = net_ip_parse(pkt, 28, &proto, &src, &dst);
        if (off != 20 || proto != NET_PROTO_ICMP ||
            src != NET_ADDR_LOOPBACK || dst != NET_ADDR_LOOPBACK) {
            serial_puts("NET-FAIL ip-parse\n");
            return -1;
        }
        /* Corrupt one header byte: parse must refuse. */
        pkt[9] ^= 0xFFu;
        if (net_ip_parse(pkt, 28, &proto, &src, &dst) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL ip-mutate\n");
            return -1;
        }
        pkt[9] ^= 0xFFu;
    }
    /* ICMP build -> check round-trip (request fails check, reply
     * passes: type discipline). */
    {
        uint8_t pay[4] = { 1, 2, 3, 4 };
        int cl = net_icmp_build(pkt, 0xA0u, 7, pay, 4);
        if (cl != 12) {
            serial_puts("NET-FAIL icmp-build\n");
            return -1;
        }
        if (net_icmp_check(pkt, 12, 0xA0u, 7) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL icmp-type\n");
            return -1;
        }
        pkt[0] = 0;
        wr16(pkt + 2, 0);
        wr16(pkt + 2, net_checksum(pkt, 12));
        if (net_icmp_check(pkt, 12, 0xA0u, 7) != 0) {
            serial_puts("NET-FAIL icmp-check\n");
            return -1;
        }
        if (net_icmp_check(pkt, 12, 0xA0u, 8) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL icmp-seq\n");
            return -1;
        }
    }
    /* Loopback empty discipline. */
    if (net_loop_rx(pkt, sizeof(pkt)) != -NOVA_EAGAIN) {
        serial_puts("NET-FAIL loop-empty\n");
        return -1;
    }
    /* End-to-end ping (payload verified, RTT sane). */
    rtt = net_ping(NET_ADDR_LOOPBACK, 1);
    if (rtt < 0) {
        serial_puts("NET-FAIL ping\n");
        return -1;
    }
    /* Unroutable destination: honest errno, not silence. */
    if (net_ping(0x0A000001u, 1) != -NOVA_EHOSTUNREACH) {
        serial_puts("NET-FAIL noroute\n");
        return -1;
    }
    {
        uint32_t tx, rx;
        net_loop_stats(&tx, &rx);
        if (tx == 0 || rx == 0 || tx != rx) {
            serial_puts("NET-FAIL stats\n");
            return -1;
        }
    }
    serial_puts("NET-OK\n");
    return NOVA_ESUCCESS;
}
