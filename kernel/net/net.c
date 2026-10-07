/* NOVA OS IPv4 + ICMP + UDP + DHCP (Phase 9c).
 * Big-endian wire order throughout (shifts, never casts). Loopback
 * is synchronous memory (tx copies into the rx slot); the wire goes
 * via Ethernet + ARP through virtio-net descriptor rings. Runtime
 * IP config (DHCP overwrites the VBox NAT defaults below).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "net/net.h"
#include "net/sock.h"
#include "drivers/virtio_net.h"
#include "syscall/syscall.h"
#include "time/time.h"

/* Runtime config (init = VBox NAT defaults; dhcp_acquire sets). */
static uint32_t g_self_ip = NET_ADDR_SELF;
static uint32_t g_gw_ip = NET_ADDR_GW;
static uint32_t g_net_mask = NET_ADDR_MASK;
static uint32_t g_lease_secs;

uint32_t net_self_ip(void) {
    return g_self_ip;
}

uint32_t net_gw_ip(void) {
    return g_gw_ip;
}

uint32_t net_netmask(void) {
    return g_net_mask;
}

static void eth_build(uint8_t *out, const uint8_t *dst,
                      const uint8_t *src, uint16_t type,
                      const uint8_t *payload, uint32_t len);
int eth_parse(const uint8_t *frame, uint32_t len,
              const uint8_t *mymac, uint16_t *type_out);
static int arp_resolve(uint32_t ip, uint8_t *mac_out);
static int rx_wait(uint8_t *buf, uint32_t max, uint32_t timeout,
                   uint32_t *len_out);

#define ETH_TYPE_ARP 0x0806u
#define ARP_OP_REQ 1u
#define ARP_OP_REP 2u

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

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint32_t)p[0] << 8 | p[1]);
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
    /* No reassembly, ever: reserved/MF/offset refuse (DF is fine,
     * offset zero required). Dead TTL refuses (nothing left to
     * route with). */
    {
        uint32_t frag = ((uint32_t)pkt[6] << 8) | pkt[7];
        if ((frag & 0x8000u) != 0 || (frag & 0x2000u) != 0 ||
            (frag & 0x1FFFu) != 0) {
            return -NOVA_EINVAL;
        }
    }
    if (pkt[8] == 0) {
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

/* Wire ping (Phase 9b): on-link or gateway destinations via
 * Ethernet+ARP; off-subnet is EHOSTUNREACH (no routing yet).
 * Returns RTT ticks or negative -errno (ETIMEDOUT on silence). */
static int32_t ping_wire(uint32_t addr, uint16_t seq) {
    static uint8_t tx[512];
    static uint8_t rx[512];
    static uint8_t frame[1600];
    uint8_t payload[32];
    uint8_t dmac[6];
    const uint8_t *mymac;
    uint32_t t0;
    int32_t dt;
    int iplen, iclen;
    uint32_t i;
    uint32_t rc;
    if (!virtio_net_ready()) {
        return -NOVA_EHOSTUNREACH;
    }
    if (addr != g_gw_ip &&
        (addr & g_net_mask) != (g_self_ip & g_net_mask)) {
        return -NOVA_EHOSTUNREACH;
    }
    rc = (uint32_t)arp_resolve(addr, dmac);
    if ((int32_t)rc != 0) {
        return (int32_t)rc;
    }
    mymac = virtio_net_mac();
    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i & 0xFFu);
    }
    iclen = net_icmp_build(tx + 20, 0xB0u, seq, payload,
                           sizeof(payload));
    if (iclen < 0) {
        return iclen;
    }
    iplen = net_ip_build(tx, g_self_ip, addr, NET_PROTO_ICMP,
                         (uint16_t)iclen);
    if (iplen < 0) {
        return iplen;
    }
    eth_build(frame, dmac, mymac, ETH_TYPE_IP, tx,
              (uint32_t)iplen + (uint32_t)iclen);
    t0 = g_ticks;
    if (virtio_net_tx(frame, 14u + (uint32_t)iplen + (uint32_t)iclen) !=
        0) {
        return -NOVA_EIO;
    }
    for (;;) {
        uint32_t got = 0;
        uint16_t type = 0;
        int er;
        uint8_t proto = 0;
        uint32_t src = 0, dst = 0;
        int off;
        rc = (uint32_t)rx_wait(rx, sizeof(rx), 300, &got);
        if ((int32_t)rc != 0) {
            return (int32_t)rc;
        }
        er = eth_parse(rx, got, mymac, &type);
        if (er < 0 || type != ETH_TYPE_IP) {
            continue;
        }
        off = net_ip_parse(rx + 14, got - 14u, &proto, &src, &dst);
        if (off < 0 || proto != NET_PROTO_ICMP || dst != g_self_ip ||
            src != addr) {
            continue;
        }
        if (net_icmp_check(rx + 14u + (uint32_t)off,
                           got - 14u - (uint32_t)off, 0xB0u, seq) !=
            0) {
            continue;
        }
        for (i = 0; i < sizeof(payload); i++) {
            if (rx[14u + (uint32_t)off + 8u + i] != payload[i]) {
                break;
            }
        }
        if (i != sizeof(payload)) {
            continue;
        }
        break;
    }
    dt = (int32_t)(g_ticks - t0);
    return (dt < 0) ? 0 : dt;
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
        return ping_wire(addr, seq);
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

/* ---------- Ethernet + ARP + wire ping (Phase 9b) ----------
 * Static config (VBox NAT defaults; DHCP in 9c). One-entry ARP
 * cache (the gateway: the only peer we talk to). All wire input is
 * untrusted: lengths, senders, and opcodes validated before use
 * (threat model actor 4). RX polls with tick timeouts (no IRQ yet).
 */

static uint8_t g_gw_mac[6];
static int g_gw_known;

static void eth_build(uint8_t *out, const uint8_t *dst,
                      const uint8_t *src, uint16_t type,
                      const uint8_t *payload, uint32_t len) {
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        out[i] = dst[i];
        out[6u + i] = src[i];
    }
    out[12] = (uint8_t)(type >> 8);
    out[13] = (uint8_t)(type & 0xFFu);
    for (i = 0; i < len; i++) {
        out[14u + i] = payload[i];
    }
}

/* Parse one RX frame: returns payload offset or -errno; drops
 * anything malformed, non-ARP/IP, or not for us (quietly). */
int eth_parse(const uint8_t *frame, uint32_t len,
                     const uint8_t *mymac, uint16_t *type_out) {
    uint32_t i;
    int mine = 1;
    int bcast = 1;
    if (frame == 0 || len < 14u) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < 6u; i++) {
        if (frame[i] != mymac[i]) {
            mine = 0;
        }
        if (frame[i] != 0xFFu) {
            bcast = 0;
        }
    }
    if (!mine && !bcast) {
        return -NOVA_EINVAL;
    }
    if (type_out != 0) {
        *type_out = (uint16_t)((uint32_t)frame[12] << 8 | frame[13]);
    }
    return 14;
}

static void arp_build(uint8_t *out, uint16_t op, const uint8_t *smac,
                      uint32_t sip, const uint8_t *tmac, uint32_t tip) {
    uint32_t i;
    out[0] = 0;
    out[1] = 1; /* Ethernet */
    out[2] = 8;
    out[3] = 0; /* IPv4 */
    out[4] = 6;
    out[5] = 4;
    out[6] = (uint8_t)(op >> 8);
    out[7] = (uint8_t)(op & 0xFFu);
    for (i = 0; i < 6u; i++) {
        out[8u + i] = smac[i];
    }
    wr32(out + 14, sip);
    for (i = 0; i < 6u; i++) {
        out[18u + i] = tmac[i];
    }
    wr32(out + 24, tip);
}

/* Validate an ARP reply for us: opcode, target match, sender sane.
 * Copies sender MAC on success. */
static int arp_check(const uint8_t *pkt, uint32_t len,
                     const uint8_t *mymac, uint32_t myip,
                     uint32_t want_ip, uint8_t *mac_out) {
    uint32_t i;
    if (pkt == 0 || len < 28u) {
        return -NOVA_EINVAL;
    }
    if (pkt[0] != 0 || pkt[1] != 1 || pkt[2] != 8 || pkt[3] != 0 ||
        pkt[4] != 6 || pkt[5] != 4) {
        return -NOVA_EINVAL;
    }
    if (pkt[6] != 0 || pkt[7] != ARP_OP_REP) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < 6u; i++) {
        if (pkt[18u + i] != mymac[i]) {
            return -NOVA_EINVAL;
        }
    }
    if (rd32(pkt + 24) != myip) {
        return -NOVA_EINVAL;
    }
    if (rd32(pkt + 14) != want_ip) {
        return -NOVA_EINVAL;
    }
    if ((pkt[8] & 1u) != 0) {
        return -NOVA_EINVAL; /* multicast sender: nonsense */
    }
    for (i = 0; i < 6u; i++) {
        mac_out[i] = pkt[8u + i];
    }
    return NOVA_ESUCCESS;
}

/* Poll one RX frame with a tick timeout (ETIMEDOUT on silence).
 * Single ingress: the pump moves ring frames to socket slots or
 * the pending queue; this waits on the queue head (FIFO order and
 * consume-on-toobig match the old direct-ring behavior exactly).
 * Queue depth 4 covers request/reply flows; floods drop newest. */
#define NET_RXQ_N 4u
#define NET_RXQ_FRAME 1600u

static uint8_t net_rxq[NET_RXQ_N][NET_RXQ_FRAME];
static uint32_t net_rxq_len[NET_RXQ_N];
static uint32_t net_rxq_n;

/* Drop the queue head (consumed or too big: legacy consumed
 * oversize ring frames the same way). */
static void net_rxq_drop(void) {
    for (uint32_t k = 0; k + 1u < net_rxq_n; k++) {
        net_rxq_len[k] = net_rxq_len[k + 1u];
        for (uint32_t j = 0; j < net_rxq_len[k]; j++) {
            net_rxq[k][j] = net_rxq[k + 1u][j];
        }
    }
    if (net_rxq_n != 0) {
        net_rxq_n--;
    }
}

int net_rx_pump_locked(void) {
    static uint8_t frame[NET_RXQ_FRAME];
    int diverted = 0;
    for (;;) {
        int rc = virtio_net_rx(frame, sizeof(frame));
        if (rc == -NOVA_EAGAIN) {
            break;
        }
        if (rc < 0) {
            break; /* ring error ends the drain (count kept) */
        }
        if (sock_demux_frame(frame, (uint32_t)rc) == 1) {
            diverted++;
        } else if (net_rxq_n < NET_RXQ_N) {
            uint32_t n = (uint32_t)rc;
            for (uint32_t k = 0; k < n; k++) {
                net_rxq[net_rxq_n][k] = frame[k];
            }
            net_rxq_len[net_rxq_n] = n;
            net_rxq_n++;
        }
        /* Else: queue full, newest drops (documented). */
    }
    return diverted;
}

int net_rx_pump(void) {
    int n;
    net_rx_lock();
    n = net_rx_pump_locked();
    net_rx_unlock();
    return n;
}

static int rx_wait(uint8_t *buf, uint32_t max, uint32_t timeout,
                   uint32_t *len_out) {
    uint32_t t0 = g_ticks;
    if (buf == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    /* Pump self-locks; the serve below takes the lock separately:
     * an IRQ drain between the two only appends (FIFO order kept),
     * never interleaves a critical section. */
    for (;;) {
        uint32_t n;
        uint32_t k;
        int served = 0;
        int big = 0;
        net_rx_pump();
        net_rx_lock();
        if (net_rxq_n != 0) {
            n = net_rxq_len[0];
            if (n > max) {
                /* Consume + refuse (legacy: the ring did this). */
                net_rxq_drop();
                big = 1;
            } else {
                for (k = 0; k < n; k++) {
                    buf[k] = net_rxq[0][k];
                }
                net_rxq_drop();
                *len_out = n;
                served = 1;
            }
        }
        net_rx_unlock();
        if (big) {
            return -NOVA_ENOSPC;
        }
        if (served) {
            return NOVA_ESUCCESS;
        }
        if (g_ticks - t0 >= timeout) {
            return -NOVA_ETIMEDOUT;
        }
    }
}

/* Resolve an on-link IPv4 address via ARP (request + poll).
 * Caches the gateway (only peer we keep). */
static int arp_resolve(uint32_t ip, uint8_t *mac_out) {
    static uint8_t tx[128];
    static uint8_t rx[512];
    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF };
    static const uint8_t zero[6] = { 0, 0, 0, 0, 0, 0 };
    const uint8_t *mymac;
    uint8_t arp[28];
    uint32_t i;
    if (mac_out == 0 || !virtio_net_ready()) {
        return -NOVA_EINVAL;
    }
    if (ip == g_gw_ip && g_gw_known) {
        for (i = 0; i < 6u; i++) {
            mac_out[i] = g_gw_mac[i];
        }
        return NOVA_ESUCCESS;
    }
    mymac = virtio_net_mac();
    arp_build(arp, ARP_OP_REQ, mymac, g_self_ip, zero, ip);
    eth_build(tx, bcast, mymac, ETH_TYPE_ARP, arp, 28);
    if (virtio_net_tx(tx, 42) != 0) {
        return -NOVA_EIO;
    }
    for (;;) {
        uint32_t got = 0;
        uint16_t type = 0;
        int rc = rx_wait(rx, sizeof(rx), 200, &got);
        uint8_t smac[6];
        if (rc != 0) {
            return rc;
        }
        if (eth_parse(rx, got, mymac, &type) < 0 ||
            type != ETH_TYPE_ARP) {
            continue;
        }
        if (arp_check(rx + 14, got - 14u, mymac, g_self_ip, ip,
                      smac) != 0) {
            continue;
        }
        for (i = 0; i < 6u; i++) {
            mac_out[i] = smac[i];
            if (ip == g_gw_ip) {
                g_gw_mac[i] = smac[i];
            }
        }
        if (ip == g_gw_ip) {
            g_gw_known = 1;
        }
        return NOVA_ESUCCESS;
    }
}

/* ---------- UDP (Phase 9c, wire only) ----------
 * Datagrams over the wire path (ARP + IP + Ethernet). No loopback
 * UDP (nothing serves it) and no sockets yet: send/recv are
 * synchronous countdowns, ports filter inbound. Checksums use the
 * IPv4 pseudo-header; a zero RX checksum means absent (RFC 768).
 */

static uint32_t cksum_add(uint32_t sum, const uint8_t *p, uint32_t n) {
    uint32_t i = 0;
    while (i + 1u < n) {
        sum += ((uint32_t)p[i] << 8) | p[i + 1u];
        if (sum > 0xFFFFu) {
            sum = (sum & 0xFFFFu) + 1u;
        }
        i += 2u;
    }
    if (i < n) {
        sum += (uint32_t)p[i] << 8;
        if (sum > 0xFFFFu) {
            sum = (sum & 0xFFFFu) + 1u;
        }
    }
    return sum;
}

/* UDP checksum over pseudo-header + datagram (cksum field as
 * stored: zero it before calling for build). */
static uint16_t udp_cksum(uint32_t src, uint32_t dst,
                          const uint8_t *u, uint32_t ulen) {
    uint8_t pseudo[12];
    uint32_t sum;
    wr32(pseudo, src);
    wr32(pseudo + 4, dst);
    pseudo[8] = 0;
    pseudo[9] = NET_PROTO_UDP;
    pseudo[10] = (uint8_t)(ulen >> 8);
    pseudo[11] = (uint8_t)(ulen & 0xFFu);
    sum = cksum_add(0, pseudo, 12);
    sum = cksum_add(sum, u, ulen);
    return (uint16_t)(~sum & 0xFFFFu);
}

int net_udp_build(uint8_t *out, uint32_t src, uint32_t dst,
                  uint16_t sport, uint16_t dport,
                  const uint8_t *payload, uint32_t payload_len) {
    uint32_t total;
    uint16_t c;
    uint32_t i;
    if (out == 0 || (payload == 0 && payload_len != 0)) {
        return -NOVA_EINVAL;
    }
    total = 8u + payload_len;
    if (total < payload_len || total + 20u > NET_MTU) {
        return -NOVA_EINVAL;
    }
    wr16(out, sport);
    wr16(out + 2, dport);
    wr16(out + 4, (uint16_t)total);
    wr16(out + 6, 0);
    for (i = 0; i < payload_len; i++) {
        out[8u + i] = payload[i];
    }
    c = udp_cksum(src, dst, out, total);
    if (c == 0) {
        c = 0xFFFFu; /* 0 means absent; a computed 0 ships as all-ones */
    }
    wr16(out + 6, c);
    return (int)total;
}

int net_udp_parse(const uint8_t *pkt, uint32_t len, uint32_t src,
                  uint32_t dst, uint16_t *sport_out,
                  uint16_t *dport_out) {
    uint32_t ulen;
    uint16_t stored;
    if (pkt == 0 || len < 8u) {
        return -NOVA_EINVAL;
    }
    ulen = ((uint32_t)pkt[4] << 8) | pkt[5];
    if (ulen < 8u || ulen > len) {
        return -NOVA_EINVAL;
    }
    stored = (uint16_t)((uint32_t)pkt[6] << 8 | pkt[7]);
    if (stored != 0 && udp_cksum(src, dst, pkt, ulen) != 0) {
        return -NOVA_EINVAL;
    }
    if (sport_out != 0) {
        *sport_out = (uint16_t)((uint32_t)pkt[0] << 8 | pkt[1]);
    }
    if (dport_out != 0) {
        *dport_out = (uint16_t)((uint32_t)pkt[2] << 8 | pkt[3]);
    }
    return 8;
}

/* Send one datagram (ARP unless broadcast). Stack stays under the
 * 512B cap: frames live in statics. */
int net_udp_send(uint32_t src, uint32_t dst, uint16_t sport,
                 uint16_t dport, const uint8_t *payload,
                 uint32_t len) {
    static uint8_t ubuf[1480];
    static uint8_t ipbuf[1500];
    static uint8_t ebuf[1528];
    const uint8_t *mymac;
    uint8_t dmac[6];
    int ulen, iplen;
    uint32_t i;
    uint32_t total;
    if (!virtio_net_ready()) {
        return -NOVA_EINVAL;
    }
    if (dst != NET_ADDR_BCAST &&
        (dst & g_net_mask) != (g_self_ip & g_net_mask)) {
        return -NOVA_EHOSTUNREACH;
    }
    ulen = net_udp_build(ubuf, src, dst, sport, dport, payload,
                         len);
    if (ulen < 0) {
        return ulen;
    }
    iplen = net_ip_build(ipbuf, src, dst, NET_PROTO_UDP,
                         (uint16_t)ulen);
    if (iplen < 0) {
        return iplen;
    }
    total = (uint32_t)iplen + (uint32_t)ulen;
    for (i = 0; i < (uint32_t)ulen; i++) {
        ipbuf[iplen + i] = ubuf[i];
    }
    if (dst == NET_ADDR_BCAST) {
        for (i = 0; i < 6u; i++) {
            dmac[i] = 0xFFu;
        }
    } else if (arp_resolve(dst, dmac) != 0) {
        return -NOVA_EIO;
    }
    mymac = virtio_net_mac();
    eth_build(ebuf, dmac, mymac, ETH_TYPE_IP, ipbuf, total);
    if (virtio_net_tx(ebuf, 14u + total) != 0) {
        return -NOVA_EIO;
    }
    return NOVA_ESUCCESS;
}

/* Receive one datagram for (sport,dport), copying the payload.
 * dst_ok = 0 accepts any IP destination (pre-config acquisition);
 * otherwise the IP dst must match. ETIMEDOUT on silence. */
int net_udp_recv(uint8_t *out, uint32_t max, uint16_t sport,
                 uint16_t dport, uint32_t dst_ok, uint32_t timeout,
                 uint32_t *len_out) {
    static uint8_t rx[1600];
    const uint8_t *mymac;
    if (out == 0 || len_out == 0 || !virtio_net_ready()) {
        return -NOVA_EINVAL;
    }
    mymac = virtio_net_mac();
    for (;;) {
        uint32_t got = 0;
        uint16_t type = 0;
        uint8_t proto = 0;
        uint32_t src = 0, dst = 0;
        uint16_t gs = 0, gd = 0;
        uint32_t ulen;
        uint32_t n;
        int off;
        int rc = rx_wait(rx, sizeof(rx), timeout, &got);
        if (rc != 0) {
            return rc;
        }
        if (eth_parse(rx, got, mymac, &type) < 0 ||
            type != ETH_TYPE_IP) {
            continue;
        }
        off = net_ip_parse(rx + 14, got - 14u, &proto, &src,
                           &dst);
        if (off < 0 || proto != NET_PROTO_UDP) {
            continue;
        }
        if (dst_ok != 0 && dst != dst_ok &&
            dst != NET_ADDR_BCAST) {
            continue;
        }
        if (net_udp_parse(rx + 14u + (uint32_t)off,
                          got - 14u - (uint32_t)off, src, dst,
                          &gs, &gd) != 8) {
            continue;
        }
        if (gd != dport || (sport != 0 && gs != sport)) {
            continue;
        }
        ulen = ((uint32_t)rx[14u + (uint32_t)off + 4u] << 8) |
               rx[14u + (uint32_t)off + 5u];
        if (ulen < 8u || ulen - 8u > max) {
            return -NOVA_ENOSPC;
        }
        for (n = 0; n < ulen - 8u; n++) {
            out[n] = rx[14u + (uint32_t)off + 8u + n];
        }
        *len_out = ulen - 8u;
        return NOVA_ESUCCESS;
    }
}

/* ---------- DHCP client (Phase 9c, RFC 2131 subset) ----------
 * DISCOVER -> OFFER -> REQUEST -> ACK/NAK over broadcast UDP
 * (sport 68, dport 67), bounded retries, then the runtime config
 * applies. Servers are untrusted (threat model actor 4): every
 * option length is bounds-checked, XID/chaddr/magic must match,
 * and the offered address + mask must be sane before use.
 */

#define DHCP_OP_REQ 1u
#define DHCP_OP_REP 2u
#define DHCP_MSG_DISCOVER 1u
#define DHCP_MSG_OFFER 2u
#define DHCP_MSG_REQUEST 3u
#define DHCP_MSG_ACK 5u
#define DHCP_MSG_NAK 6u
#define DHCP_OPT_MASK 1u
#define DHCP_OPT_ROUTER 3u
#define DHCP_OPT_REQIP 50u
#define DHCP_OPT_LEASE 51u
#define DHCP_OPT_MSGTYPE 53u
#define DHCP_OPT_SERVER 54u
#define DHCP_OPT_PRM 55u
#define DHCP_OPT_CLIENTID 61u
#define DHCP_OPT_END 255u
#define DHCP_MIN_MSG 300u

/* Contiguous-ones mask, neither empty nor /32 (a /32 breaks the
 * ARP model: no on-link peers at all). */
static int mask_sane(uint32_t m) {
    uint32_t host;
    if (m == 0 || m == 0xFFFFFFFFu) {
        return -NOVA_EINVAL;
    }
    host = ~m;
    if ((host & (host + 1u)) != 0) {
        return -NOVA_EINVAL;
    }
    return NOVA_ESUCCESS;
}

/* Unicast, non-loopback; with a mask, not the wire or broadcast
 * address of that subnet either. */
static int yiaddr_sane(uint32_t y, uint32_t mask, int have_mask) {
    uint32_t host;
    if (y == 0 || y == 0xFFFFFFFFu) {
        return -NOVA_EINVAL;
    }
    if ((y >> 28) >= 0x0Eu) {
        return -NOVA_EINVAL;
    }
    if ((y >> 24) == 0x7Fu) {
        return -NOVA_EINVAL;
    }
    if (have_mask) {
        host = y & ~mask;
        if (host == 0 || host == ~mask) {
            return -NOVA_EINVAL;
        }
    }
    return NOVA_ESUCCESS;
}

static void dhcp_wr_opt(uint8_t *o, uint32_t *pos, uint8_t code,
                        uint8_t len, const uint8_t *val) {
    uint32_t i;
    o[*pos] = code;
    o[*pos + 1u] = len;
    for (i = 0; i < len; i++) {
        o[*pos + 2u + i] = val[i];
    }
    *pos += 2u + len;
}

/* Build a DISCOVER/REQUEST into out (padded to 300B). req_ip and
 * server_ip are the OFFERed values (REQUEST only). */
static int dhcp_build(uint8_t *out, uint8_t msgtype, uint32_t xid,
                      const uint8_t *mac, uint32_t req_ip,
                      uint32_t server_ip, uint32_t secs) {
    uint32_t p;
    uint32_t i;
    uint8_t mt = msgtype;
    uint8_t cid[7];
    uint8_t rip[4];
    uint8_t sid[4];
    uint8_t prm[3] = { 1, 3, 6 };
    if (out == 0 || mac == 0 ||
        (msgtype != DHCP_MSG_DISCOVER &&
         msgtype != DHCP_MSG_REQUEST)) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < 236u; i++) {
        out[i] = 0;
    }
    out[0] = DHCP_OP_REQ;
    out[1] = 1;
    out[2] = 6;
    out[3] = 0;
    wr32(out + 4, xid);
    wr16(out + 8, (uint16_t)(secs > 0xFFFFu ? 0xFFFFu : secs));
    wr16(out + 10, 0x8000u); /* broadcast flag: answer loud */
    for (i = 0; i < 6u; i++) {
        out[28u + i] = mac[i];
    }
    out[236] = 99;
    out[237] = 130;
    out[238] = 83;
    out[239] = 99;
    p = 240u;
    dhcp_wr_opt(out, &p, DHCP_OPT_MSGTYPE, 1, &mt);
    cid[0] = 1;
    for (i = 0; i < 6u; i++) {
        cid[1u + i] = mac[i];
    }
    dhcp_wr_opt(out, &p, DHCP_OPT_CLIENTID, 7, cid);
    if (msgtype == DHCP_MSG_REQUEST) {
        wr32(rip, req_ip);
        dhcp_wr_opt(out, &p, DHCP_OPT_REQIP, 4, rip);
        wr32(sid, server_ip);
        dhcp_wr_opt(out, &p, DHCP_OPT_SERVER, 4, sid);
    }
    dhcp_wr_opt(out, &p, DHCP_OPT_PRM, 3, prm);
    out[p] = DHCP_OPT_END;
    p++;
    while (p < DHCP_MIN_MSG) {
        out[p] = 0;
        p++;
    }
    return (int)DHCP_MIN_MSG;
}

int dhcp_parse_msg(const uint8_t *pkt, uint32_t len, uint32_t xid,
                   const uint8_t *chmac, uint8_t *msgtype_out,
                   uint32_t *yiaddr_out, uint32_t *server_out,
                   uint32_t *mask_out, uint32_t *router_out,
                   uint32_t *lease_out) {
    uint32_t p;
    uint32_t i;
    uint8_t mt = 0;
    int have_mt = 0;
    uint32_t srv = 0;
    int have_srv = 0;
    uint32_t mask = 0;
    int have_mask = 0;
    uint32_t router = 0;
    int have_router = 0;
    uint32_t lease = 0;
    uint32_t y;
    if (pkt == 0 || chmac == 0 || msgtype_out == 0 ||
        yiaddr_out == 0 || server_out == 0 || mask_out == 0 ||
        router_out == 0 || lease_out == 0) {
        return -NOVA_EINVAL;
    }
    if (len < 240u) {
        return -NOVA_EINVAL;
    }
    if (pkt[0] != DHCP_OP_REP || pkt[1] != 1 || pkt[2] != 6 ||
        pkt[3] != 0) {
        return -NOVA_EINVAL;
    }
    if (rd32(pkt + 4) != xid) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < 6u; i++) {
        if (pkt[28u + i] != chmac[i]) {
                return -NOVA_EINVAL;
        }
    }
    /* RFC 2132 magic cookie 99.130.83.99 (NOT hex 63..: the
     * option walk keys off these exact decimal bytes). */
    if (pkt[236] != 99 || pkt[237] != 130 || pkt[238] != 83 ||
        pkt[239] != 99) {
        return -NOVA_EINVAL;
    }
    p = 240u;
    while (p < len) {
        uint8_t code = pkt[p];
        uint8_t ol;
        if (code == DHCP_OPT_END) {
            break;
        }
        if (code == 0) {
            p++;
            continue;
        }
        if (p + 1u >= len) {
                return -NOVA_EINVAL;
        }
        ol = pkt[p + 1u];
        if (p + 2u + (uint32_t)ol > len) {
                return -NOVA_EINVAL;
        }
        switch (code) {
        case DHCP_OPT_MSGTYPE:
            if (ol == 1) {
                mt = pkt[p + 2u];
                have_mt = 1;
            }
            break;
        case DHCP_OPT_SERVER:
            if (ol == 4) {
                srv = rd32(pkt + p + 2u);
                have_srv = 1;
            }
            break;
        case DHCP_OPT_MASK:
            if (ol == 4) {
                uint32_t m = rd32(pkt + p + 2u);
                if (mask_sane(m) == 0) {
                    mask = m;
                    have_mask = 1;
                }
            }
            break;
        case DHCP_OPT_ROUTER:
            if (ol >= 4 && !have_router) {
                router = rd32(pkt + p + 2u);
                have_router = 1;
            }
            break;
        case DHCP_OPT_LEASE:
            if (ol == 4) {
                lease = rd32(pkt + p + 2u);
            }
            break;
        default:
            break;
        }
        p += 2u + (uint32_t)ol;
    }
    if (!have_mt ||
        (mt != DHCP_MSG_OFFER && mt != DHCP_MSG_ACK &&
         mt != DHCP_MSG_NAK)) {
        return -NOVA_EINVAL;
    }
    *msgtype_out = mt;
    if (mt == DHCP_MSG_NAK) {
        return NOVA_ESUCCESS; /* explicit refusal: nothing to trust */
    }
    if (!have_srv) {
        return -NOVA_EINVAL;
    }
    y = rd32(pkt + 16);
    if (yiaddr_sane(y, mask, have_mask) != 0) {
        return -NOVA_EINVAL;
    }
    *yiaddr_out = y;
    *server_out = srv;
    *mask_out = have_mask ? mask : g_net_mask;
    *router_out = have_router ? router : srv;
    *lease_out = lease;
    return NOVA_ESUCCESS;
}

static void print_ip(uint32_t ip) {
    for (int q = 3; q >= 0; q--) {
        serial_putdec32((ip >> (uint32_t)(q * 8)) & 0xFFu);
        if (q != 0) {
            serial_putc('.');
        }
    }
}

/* Commit a validated lease (gateway moves -> re-ARP). */
static void dhcp_apply(uint32_t y, uint32_t m, uint32_t r,
                       uint32_t l) {
    g_self_ip = y;
    g_net_mask = m;
    g_gw_ip = r;
    g_lease_secs = l;
    g_gw_known = 0;
    serial_puts("DHCP-OK ip=");
    print_ip(g_self_ip);
    serial_puts(" mask=");
    print_ip(g_net_mask);
    serial_puts(" gw=");
    print_ip(g_gw_ip);
    serial_puts(" lease=");
    serial_putdec32(g_lease_secs);
    serial_putc('\n');
}

/* Bounded acquisition: 2 rounds of (DISCOVER x2 -> REQUEST x2).
 * Worst case ~16s of silence; the live server answers in ms. */
int dhcp_acquire(void) {
    static uint8_t msg[320];
    static uint8_t udp[1500];
    const uint8_t *mymac;
    int round;
    if (!virtio_net_ready()) {
        return -NOVA_ENODEV;
    }
    mymac = virtio_net_mac();
    for (round = 0; round < 2; round++) {
        uint32_t xid =
            (g_ticks << 16) ^
            ((uint32_t)mymac[4] << 8 | mymac[5]) ^ (uint32_t)round;
        uint32_t t0 = g_ticks;
        uint32_t offered = 0, server = 0;
        int have_offer = 0;
        int t;
        for (t = 0; t < 2 && !have_offer; t++) {
            uint32_t got = 0;
            uint8_t mt = 0;
            uint32_t y = 0, s = 0, m = 0, r = 0, l = 0;
            if (dhcp_build(msg, DHCP_MSG_DISCOVER, xid, mymac,
                           0, 0,
                           (g_ticks - t0) / 100u) < 0) {
                return -NOVA_EIO;
            }
            if (net_udp_send(0, NET_ADDR_BCAST,
                             NET_PORT_DHCP_CLIENT,
                             NET_PORT_DHCP_SERVER, msg,
                             DHCP_MIN_MSG) != 0) {
                return -NOVA_EIO;
            }
            if (net_udp_recv(udp, sizeof(udp),
                             NET_PORT_DHCP_SERVER,
                             NET_PORT_DHCP_CLIENT, 0, 200,
                             &got) != 0) {
                continue;
            }
            if (dhcp_parse_msg(udp, got, xid, mymac, &mt, &y,
                               &s, &m, &r, &l) != 0) {
                continue;
            }
            /* Rapid-commit style: some servers (VBox NAT) answer
             * DISCOVER with a full ACK. XID-matched and fully
             * validated, it commits directly. */
            if (mt == DHCP_MSG_ACK) {
                dhcp_apply(y, m, r, l);
                return NOVA_ESUCCESS;
            }
            if (mt != DHCP_MSG_OFFER) {
                continue;
            }
            offered = y;
            server = s;
            have_offer = 1;
        }
        if (!have_offer) {
            continue;
        }
        for (t = 0; t < 2; t++) {
            uint32_t got = 0;
            uint8_t mt = 0;
            uint32_t y = 0, s = 0, m = 0, r = 0, l = 0;
            if (dhcp_build(msg, DHCP_MSG_REQUEST, xid, mymac,
                           offered, server,
                           (g_ticks - t0) / 100u) < 0) {
                return -NOVA_EIO;
            }
            if (net_udp_send(0, NET_ADDR_BCAST,
                             NET_PORT_DHCP_CLIENT,
                             NET_PORT_DHCP_SERVER, msg,
                             DHCP_MIN_MSG) != 0) {
                return -NOVA_EIO;
            }
            if (net_udp_recv(udp, sizeof(udp),
                             NET_PORT_DHCP_SERVER,
                             NET_PORT_DHCP_CLIENT, 0, 200,
                             &got) != 0) {
                continue;
            }
            if (dhcp_parse_msg(udp, got, xid, mymac, &mt, &y,
                               &s, &m, &r, &l) != 0) {
                continue;
            }
            if (mt == DHCP_MSG_NAK) {
                break; /* refused: fresh XID next round */
            }
            if (mt != DHCP_MSG_ACK) {
                continue;
            }
            dhcp_apply(y, m, r, l);
            return NOVA_ESUCCESS;
        }
    }
    return -NOVA_ETIMEDOUT;
}

/* ---------- DNS client (Phase 14b, RFC 1035 A subset) ----------
 * Query: RD set, one question (QTYPE A, QCLASS IN). Reply: ID +
 * QR match, RCODE discipline (3/NameError -> ENOENT, else EIO),
 * every length bounds-checked, name compression chased with a
 * jump cap (rogue servers loop pointers: actor 4). First A
 * record wins; CNAME-only answers refuse (no chain following
 * in v0.1). Names: labels 1..63 of alnum/hyphen, dots between,
 * total <= 253, no leading/trailing dot. */

static int dns_label_ok(const char *s, uint32_t n) {
    uint32_t i;
    if (n == 0 || n > 63u) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return -NOVA_EINVAL;
        }
    }
    if (s[0] == '-' || s[n - 1u] == '-') {
        return -NOVA_EINVAL;
    }
    return NOVA_ESUCCESS;
}

int dns_build(uint8_t *out, uint32_t max, uint16_t id,
              const char *name) {
    uint32_t pos = 12u;
    uint32_t total = 0;
    if (out == 0 || name == 0) {
        return -NOVA_EINVAL;
    }
    /* Validate + measure the dotted form first. */
    {
        const char *p = name;
        if (*p == 0 || *p == '.') {
            return -NOVA_EINVAL;
        }
        for (;;) {
            const char *dot = p;
            uint32_t n = 0;
            while (*dot != 0 && *dot != '.') {
                dot++;
                n++;
            }
            if (dns_label_ok(p, n) != 0) {
                return -NOVA_EINVAL;
            }
            total += 1u + n;
            if (*dot == 0) {
                break;
            }
            p = dot + 1u;
            if (*p == 0) {
                return -NOVA_EINVAL; /* trailing dot */
            }
        }
        total += 1u; /* root terminator */
        if (12u + total + 4u > max || 12u + total + 4u < total) {
            return -NOVA_EINVAL;
        }
    }
    wr16(out, id);
    wr16(out + 2, 0x0100u); /* RD set */
    wr16(out + 4, 1);       /* one question */
    wr16(out + 6, 0);
    wr16(out + 8, 0);
    wr16(out + 10, 0);
    {
        const char *p = name;
        for (;;) {
            const char *dot = p;
            uint32_t n = 0;
            uint32_t i;
            while (*dot != 0 && *dot != '.') {
                dot++;
                n++;
            }
            out[pos++] = (uint8_t)n;
            for (i = 0; i < n; i++) {
                out[pos++] = (uint8_t)p[i];
            }
            if (*dot == 0) {
                break;
            }
            p = dot + 1u;
        }
        out[pos++] = 0;
    }
    wr16(out + pos, 1); /* QTYPE A */
    wr16(out + pos + 2u, 1); /* QCLASS IN */
    return (int)(pos + 4u);
}

/* Skip a possibly-compressed name at off (bounds + jump cap).
 * Returns the offset just past it, or -errno. */
static int dns_skip_name(const uint8_t *pkt, uint32_t len,
                         uint32_t off) {
    uint32_t jumps = 0;
    uint32_t end = off;
    int jumped = 0;
    for (;;) {
        uint8_t c;
        if (off >= len) {
            return -NOVA_EINVAL;
        }
        c = pkt[off];
        if ((c & 0xC0u) == 0xC0u) {
            uint32_t ptr;
            if (off + 1u >= len) {
                return -NOVA_EINVAL;
            }
            ptr = ((uint32_t)(c & 0x3Fu) << 8) | pkt[off + 1u];
            if (ptr >= len) {
                return -NOVA_EINVAL;
            }
            if (!jumped) {
                end = off + 2u;
            }
            jumped = 1;
            off = ptr;
            if (++jumps > 8u) {
                return -NOVA_EINVAL;
            }
            continue;
        }
        if (c == 0) {
            return (int)(jumped ? end : off + 1u);
        }
        if (c > 63u || off + 1u + (uint32_t)c > len) {
            return -NOVA_EINVAL;
        }
        if (!jumped) {
            end = off + 1u + (uint32_t)c;
        }
        off += 1u + (uint32_t)c;
    }
}

int dns_parse(const uint8_t *pkt, uint32_t len, uint16_t id,
              uint32_t *ip_out) {
    uint16_t flags;
    uint16_t qd, an;
    uint32_t off;
    int r;
    if (pkt == 0 || ip_out == 0 || len < 12u) {
        return -NOVA_EINVAL;
    }
    if (rd16(pkt) != id) {
        return -NOVA_EINVAL;
    }
    flags = rd16(pkt + 2);
    if ((flags & 0x8000u) == 0 || (flags & 0x7800u) != 0) {
        return -NOVA_EINVAL; /* not a reply / bad opcode */
    }
    if ((flags & 0x000Fu) == 3u) {
        return -NOVA_ENOENT; /* NameError */
    }
    if ((flags & 0x000Fu) != 0) {
        return -NOVA_EIO;
    }
    qd = rd16(pkt + 4);
    an = rd16(pkt + 6);
    if (qd < 1u || an < 1u) {
        return -NOVA_EINVAL;
    }
    /* Skip the question(s): name + QTYPE + QCLASS each. */
    off = 12u;
    for (uint32_t q = 0; q < qd; q++) {
        r = dns_skip_name(pkt, len, off);
        if (r < 0 || (uint32_t)r + 4u > len) {
            return -NOVA_EINVAL;
        }
        off = (uint32_t)r + 4u;
    }
    /* First A/IN record wins; anything else (incl. CNAME) skips. */
    for (uint32_t a = 0; a < an; a++) {
        uint16_t type, class, rdlen;
        r = dns_skip_name(pkt, len, off);
        if (r < 0 || (uint32_t)r + 10u > len) {
            return -NOVA_EINVAL;
        }
        off = (uint32_t)r;
        type = rd16(pkt + off);
        class = rd16(pkt + off + 2u);
        rdlen = rd16(pkt + off + 8u);
        if (off + 10u + (uint32_t)rdlen > len) {
            return -NOVA_EINVAL;
        }
        if (type == 1 && class == 1 && rdlen == 4) {
            *ip_out = rd32(pkt + off + 10u);
            return NOVA_ESUCCESS;
        }
        off += 10u + (uint32_t)rdlen;
    }
    return -NOVA_ENOENT; /* no direct answer present */
}

int dns_resolve(const char *name, uint32_t *ip_out) {
    static uint8_t q[512];
    static uint8_t u[1500];
    const uint8_t *mymac;
    uint16_t id;
    uint16_t sport;
    int qlen;
    int round;
    if (name == 0 || ip_out == 0 || !virtio_net_ready()) {
        return -NOVA_EINVAL;
    }
    mymac = virtio_net_mac();
    id = (uint16_t)((g_ticks & 0xFFFFu) ^ ((uint32_t)mymac[4] << 8 |
                                           mymac[5]));
    sport = (uint16_t)(40000u + (g_ticks & 0xFFFu));
    qlen = dns_build(q, sizeof(q), id, name);
    if (qlen < 0) {
        return qlen;
    }
    /* 3 rounds: send, then drain up to 2 replies each (ID match
     * decides; anything else is noise we walk past). */
    for (round = 0; round < 3; round++) {
        if (net_udp_send(net_self_ip(), NET_ADDR_DNS, sport, 53,
                         q, (uint32_t)qlen) != 0) {
            return -NOVA_EIO;
        }
        for (int t = 0; t < 2; t++) {
            uint32_t got = 0;
            int rc = net_udp_recv(u, sizeof(u), 53, sport, 0, 100,
                                  &got);
            if (rc != 0) {
                break;
            }
            /* Ports already filtered by recv; ID decides here. */
            if (dns_parse(u, got, id, ip_out) == 0) {
                return NOVA_ESUCCESS;
            }
        }
    }
    return -NOVA_ETIMEDOUT;
}

int net_init(void) {
    g_loop_len = 0;
    g_loop_has = 0;
    g_loop_tx = 0;
    g_loop_rx = 0;
    sock_lock_init();
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
    /* UDP build -> parse round-trip (pinned checksums, executed
     * against an independent python oracle: 68->67/8B = 0xdb26,
     * zero-src/bcast = 0xff47, odd 3B = 0xd738). */
    {
        uint8_t pay[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        uint16_t sp = 0, dp = 0;
        int ul = net_udp_build(pkt, NET_ADDR_SELF, NET_ADDR_GW,
                               68, 67, pay, 8);
        if (ul != 16) {
            serial_puts("NET-FAIL udp-build\n");
            return -1;
        }
        if (pkt[6] != 0xDBu || pkt[7] != 0x26u) {
            serial_puts("NET-FAIL udp-ck\n");
            return -1;
        }
        if (net_udp_parse(pkt, 16, NET_ADDR_SELF, NET_ADDR_GW,
                          &sp, &dp) != 8 || sp != 68 || dp != 67) {
            serial_puts("NET-FAIL udp-parse\n");
            return -1;
        }
        /* Corrupt one payload byte: checksum must refuse. */
        pkt[15] ^= 0xFFu;
        if (net_udp_parse(pkt, 16, NET_ADDR_SELF, NET_ADDR_GW,
                          &sp, &dp) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL udp-mutate\n");
            return -1;
        }
        pkt[15] ^= 0xFFu;
        /* Zero checksum = absent (RFC 768): accepted, documented. */
        wr16(pkt + 6, 0);
        if (net_udp_parse(pkt, 16, NET_ADDR_SELF, NET_ADDR_GW,
                          &sp, &dp) != 8) {
            serial_puts("NET-FAIL udp-nock\n");
            return -1;
        }
        /* Length field lies (claims more than present): refuse. */
        wr16(pkt + 4, 24);
        if (net_udp_parse(pkt, 16, NET_ADDR_SELF, NET_ADDR_GW,
                          &sp, &dp) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL udp-len\n");
            return -1;
        }
    }
    /* IP completion: fragments and dead TTL refuse (DF passes). */
    {
        int hl = net_ip_build(pkt, NET_ADDR_LOOPBACK,
                              NET_ADDR_LOOPBACK, NET_PROTO_UDP, 8);
        if (hl != 20) {
            serial_puts("NET-FAIL ipf-build\n");
            return -1;
        }
        pkt[6] = 0x20u; /* MF set */
        pkt[7] = 0x00u;
        wr16(pkt + 10, 0);
        wr16(pkt + 10, net_checksum(pkt, 20));
        if (net_ip_parse(pkt, 28, &proto, &src, &dst) !=
            -NOVA_EINVAL) {
            serial_puts("NET-FAIL ipf-mf\n");
            return -1;
        }
        pkt[6] = 0x40u; /* DF only: offset must still be zero */
        pkt[7] = 0x01u;
        wr16(pkt + 10, 0);
        wr16(pkt + 10, net_checksum(pkt, 20));
        if (net_ip_parse(pkt, 28, &proto, &src, &dst) !=
            -NOVA_EINVAL) {
            serial_puts("NET-FAIL ipf-off\n");
            return -1;
        }
        pkt[7] = 0x00u;
        pkt[8] = 0; /* TTL expired in transit */
        wr16(pkt + 10, 0);
        wr16(pkt + 10, net_checksum(pkt, 20));
        if (net_ip_parse(pkt, 28, &proto, &src, &dst) !=
            -NOVA_EINVAL) {
            serial_puts("NET-FAIL ipf-ttl\n");
            return -1;
        }
    }
    {
        uint32_t tx, rx;
        net_loop_stats(&tx, &rx);
        if (tx == 0 || rx == 0 || tx != rx) {
            serial_puts("NET-FAIL stats\n");
            return -1;
        }
    }
    /* DHCP validator: crafted OFFER + mutants (rogue servers
     * are data: every field earns its trust). */
    {
        static const uint8_t tmac[6] = { 0x08, 0x00, 0x27,
                                         0x3C, 0xA1, 0xA1 };
        uint8_t mt = 0;
        uint32_t y = 0, s = 0, m = 0, r = 0, l = 0;
        uint32_t i;
        uint32_t p;
        uint8_t b1[1];
        uint8_t b4[4];
        static uint8_t offer[300];
        for (i = 0; i < sizeof(offer); i++) {
            offer[i] = 0;
        }
        offer[0] = 2;
        offer[1] = 1;
        offer[2] = 6;
        wr32(offer + 4, 0x12345678u);
        for (i = 0; i < 6u; i++) {
            offer[28u + i] = tmac[i];
        }
        wr32(offer + 16, 0x0A00020Fu);
        offer[236] = 99;
        offer[237] = 130;
        offer[238] = 83;
        offer[239] = 99;
        p = 240u;
        b1[0] = 2;
        dhcp_wr_opt(offer, &p, DHCP_OPT_MSGTYPE, 1, b1);
        wr32(b4, 0x0A000202u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_SERVER, 4, b4);
        wr32(b4, 0xFFFFFF00u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_MASK, 4, b4);
        wr32(b4, 0x0A000202u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_ROUTER, 4, b4);
        wr32(b4, 3600u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_LEASE, 4, b4);
        offer[p] = DHCP_OPT_END;
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != 0 ||
            mt != 2 || y != 0x0A00020Fu || s != 0x0A000202u ||
            m != 0xFFFFFF00u || r != 0x0A000202u || l != 3600u) {
            serial_puts("NET-FAIL dhcp-offer\n");
            return -1;
        }
        offer[4] ^= 0x01u; /* XID mismatch */
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-xid\n");
            return -1;
        }
        offer[4] ^= 0x01u;
        offer[236] = 0; /* bad magic */
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-magic\n");
            return -1;
        }
        offer[236] = 99;
        if (dhcp_parse_msg(offer, 200, 0x12345678u, tmac, &mt,
                           &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-short\n");
            return -1;
        }
        offer[28] ^= 0x01u; /* not our chaddr */
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-chaddr\n");
            return -1;
        }
        offer[28] ^= 0x01u;
        offer[240] = DHCP_OPT_END; /* type option missing */
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-notype\n");
            return -1;
        }
        offer[240] = DHCP_OPT_SERVER; /* length overruns packet */
        offer[241] = 4;
        if (dhcp_parse_msg(offer, 244, 0x12345678u, tmac, &mt,
                           &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-overrun\n");
            return -1;
        }
        /* Rebuild the valid OFFER tail the mutants above trashed. */
        p = 240u;
        b1[0] = 2;
        dhcp_wr_opt(offer, &p, DHCP_OPT_MSGTYPE, 1, b1);
        wr32(b4, 0x0A000202u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_SERVER, 4, b4);
        wr32(b4, 0xFFFFFF00u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_MASK, 4, b4);
        wr32(b4, 0x0A000202u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_ROUTER, 4, b4);
        wr32(b4, 3600u);
        dhcp_wr_opt(offer, &p, DHCP_OPT_LEASE, 4, b4);
        offer[p] = DHCP_OPT_END;
        wr32(offer + 16, 0x0A000200u); /* wire address offered */
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dhcp-wireaddr\n");
            return -1;
        }
        wr32(offer + 16, 0x0A00020Fu);
        wr32(b4, 0u); /* zero mask: ignored, current kept */
        {
            uint32_t q = 240u;
            uint8_t z1[1] = { 2 };
            dhcp_wr_opt(offer, &q, DHCP_OPT_MSGTYPE, 1, z1);
            wr32(b4, 0x0A000202u);
            dhcp_wr_opt(offer, &q, DHCP_OPT_SERVER, 4, b4);
            wr32(b4, 0u);
            dhcp_wr_opt(offer, &q, DHCP_OPT_MASK, 4, b4);
            offer[q] = DHCP_OPT_END;
        }
        if (dhcp_parse_msg(offer, sizeof(offer), 0x12345678u,
                           tmac, &mt, &y, &s, &m, &r,
                           &l) != 0 ||
            mt != 2 || m != g_net_mask) {
            serial_puts("NET-FAIL dhcp-zeromask\n");
            return -1;
        }
        { /* NAK: valid refusal, nothing else trusted */
            uint32_t q = 240u;
            uint8_t n1[1] = { 6 };
            dhcp_wr_opt(offer, &q, DHCP_OPT_MSGTYPE, 1, n1);
            offer[q] = DHCP_OPT_END;
            if (dhcp_parse_msg(offer, sizeof(offer),
                               0x12345678u, tmac, &mt, &y, &s,
                               &m, &r, &l) != 0 || mt != 6) {
                serial_puts("NET-FAIL dhcp-nak\n");
                return -1;
            }
        }
    }
    /* DNS codec (pure: runs everywhere, NIC or not). Query pin:
     * "example.com" id 0x1234 encodes to 29 exact bytes. */
    {
        static const uint8_t expect[29] = {
            0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x07, 'e', 'x', 'a',
            'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
            0x00, 0x01, 0x00, 0x01
        };
        uint8_t q[64];
        uint32_t i;
        int ql = dns_build(q, sizeof(q), 0x1234, "example.com");
        if (ql != 29) {
            serial_puts("NET-FAIL dns-len\n");
            return -1;
        }
        for (i = 0; i < 29u; i++) {
            if (q[i] != expect[i]) {
                serial_puts("NET-FAIL dns-bytes\n");
                return -1;
            }
        }
        if (dns_build(q, 10, 0x1234, "example.com") !=
                -NOVA_EINVAL ||
            dns_build(q, sizeof(q), 0x1234, "") != -NOVA_EINVAL ||
            dns_build(q, sizeof(q), 0x1234, ".lead") !=
                -NOVA_EINVAL ||
            dns_build(q, sizeof(q), 0x1234, "trail.") !=
                -NOVA_EINVAL ||
            dns_build(q, sizeof(q), 0x1234, "bad_name") !=
                -NOVA_EINVAL ||
            dns_build(0, sizeof(q), 0x1234, "example.com") !=
                -NOVA_EINVAL) {
            serial_puts("NET-FAIL dns-badname\n");
            return -1;
        }
    }
    /* DNS reply matrix: a good answer, then every rogue shape. */
    {
        uint8_t q[64];
        uint8_t r[128];
        uint32_t ql;
        uint32_t rl;
        uint32_t ip = 0;
        uint32_t i;
        int qli = dns_build(q, sizeof(q), 0x1234, "example.com");
        if (qli != 29) {
            serial_puts("NET-FAIL dns-qlen\n");
            return -1;
        }
        ql = (uint32_t)qli;
        wr16(r, 0x1234);
        wr16(r + 2, 0x8180u);
        wr16(r + 4, 1);
        wr16(r + 6, 1);
        wr16(r + 8, 0);
        wr16(r + 10, 0);
        for (i = 0; i + 12u < ql; i++) {
            r[12u + i] = q[12u + i];
        }
        rl = ql;
        {
            static const uint8_t ans[16] = {
                0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
                0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
                93, 184, 216, 34
            };
            for (i = 0; i < 16u; i++) {
                r[rl + i] = ans[i];
            }
            rl += 16u;
        }
        if (dns_parse(r, rl, 0x1234, &ip) != 0 ||
            ip != 0x5DB8D822u) {
            serial_puts("NET-FAIL dns-ok\n");
            return -1;
        }
        r[0] ^= 0x01u; /* ID mismatch */
        if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dns-id\n");
            return -1;
        }
        r[0] ^= 0x01u;
        r[3] = 0x83u; /* NXDOMAIN */
        if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_ENOENT) {
            serial_puts("NET-FAIL dns-nx\n");
            return -1;
        }
        r[3] = 0x82u; /* SERVFAIL */
        if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_EIO) {
            serial_puts("NET-FAIL dns-srvfail\n");
            return -1;
        }
        r[3] = 0x80u;
        r[2] = 0x00u; /* QR clear: not a reply */
        if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dns-qr\n");
            return -1;
        }
        r[2] = 0x81u;
        if (dns_parse(r, rl - 5u, 0x1234, &ip) != -NOVA_EINVAL) {
            serial_puts("NET-FAIL dns-trunc\n");
            return -1;
        }
        /* CNAME-only: no direct answer present. */
        r[ql] = 0xC0;
        r[ql + 1u] = 0x0C;
        r[ql + 2u] = 0x00;
        r[ql + 3u] = 0x05;
        if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_ENOENT) {
            serial_puts("NET-FAIL dns-cname\n");
            return -1;
        }
        /* Pointer loop: answer name points at itself. */
        {
            uint32_t ao = ql;
            r[ao] = 0xC0;
            r[ao + 1u] = (uint8_t)ao;
            if (dns_parse(r, rl, 0x1234, &ip) != -NOVA_EINVAL) {
                serial_puts("NET-FAIL dns-loop\n");
                return -1;
            }
        }
    }
    /* Wire proof (guest only: needs the NIC). DHCP first (dynamic
     * config), then ARP the gateway and ping it: real frames on a
     * real wire. */
    if (virtio_net_ready()) {
        uint8_t gw[6];
        int32_t wrtt;
        if (dhcp_acquire() != 0) {
            serial_puts("NET-FAIL dhcp\n");
            return -1;
        }
        if (arp_resolve(g_gw_ip, gw) != 0) {
            serial_puts("NET-FAIL arp\n");
            return -1;
        }
        if ((gw[0] & 1u) != 0) {
            serial_puts("NET-FAIL gw-mac\n");
            return -1;
        }
        wrtt = net_ping(g_gw_ip, 2);
        if (wrtt < 0) {
            serial_puts("NET-FAIL wire-ping\n");
            return -1;
        }
        /* Interrupt proof (Phase 14c): the completions above
         * (ARP + ping + DHCP + DNS TX/RX) raised IRQs through our
         * vector, acked + counted in the ISR. Skipped when the
         * BIOS left the line unrouted (polling covers). */
        if (virtio_irq_expected() && virtio_irq_count() == 0) {
            serial_puts("NET-FAIL irq-silent\n");
            return -1;
        }
        serial_puts("NET-WIRE-OK gw=");
        for (uint32_t i = 0; i < 6u; i++) {
            uint32_t b = gw[i];
            uint32_t hi = b >> 4;
            uint32_t lo = b & 0xFu;
            serial_putc((char)(hi < 10u ? '0' + hi : 'a' + hi - 10u));
            serial_putc((char)(lo < 10u ? '0' + lo : 'a' + lo - 10u));
            if (i < 5u) {
                serial_putc(':');
            }
        }
        serial_putc('\n');
        /* Opportunistic live DNS (informational only: upstream
         * reachability is environmental, never a gate). */
        {
            uint32_t ip = 0;
            if (dns_resolve("example.com", &ip) == 0) {
                serial_puts("DNS-LIVE ip=");
                print_ip(ip);
                serial_putc('\n');
            } else {
                serial_puts("DNS-NOLIVE\n");
            }
        }
    }
    serial_puts("NET-OK\n");
    return NOVA_ESUCCESS;
}
