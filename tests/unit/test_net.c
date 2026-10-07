/* NOVA OS network host test (Phase 9a, T14).
 * Host-side: the REAL kernel/net/net.c against stubs (serial,
 * g_ticks). Checksum vectors vs independent oracle, header/packet
 * build-parse round-trips, mutation discipline, loopback stats, and
 * a randomized packet fuzz for crash-freedom. VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "net/net.h"
/* mingw stdio.h already defines SYS_OPEN: drop it before our ABI. */
#undef SYS_OPEN
#include "syscall/syscall.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* --- kernel dep stubs --- */

__attribute__((noreturn)) void nova_panic(const char *reason) {
    printf("PANIC: %s\n", reason);
    exit(2);
}

void serial_puts(const char *s) {
    fputs(s, stdout);
}

void serial_putc(char c) {
    putchar(c);
}

void serial_putdec32(uint32_t v) {
    printf("%u", v);
}

void serial_puthex32(uint32_t v) {
    printf("%08x", v);
}

volatile uint32_t g_ticks = 1000u;

/* No NIC on the host: wire paths stay dormant (ready=0), which the
 * loopback tests rely on (noroute without hardware). */
int virtio_net_ready(void) {
    return 0;
}

const uint8_t *virtio_net_mac(void) {
    static uint8_t z[6] = { 0, 0, 0, 0, 0, 0 };
    return z;
}

int virtio_net_tx(const uint8_t *f, uint32_t l) {
    (void)f;
    (void)l;
    return -NOVA_EIO;
}

int virtio_net_rx(uint8_t *o, uint32_t m) {
    (void)o;
    (void)m;
    return -NOVA_EAGAIN;
}

/* No interrupts on the host (polling only): the silence assert
 * skips, exactly like an unrouted guest line. */
int virtio_irq_expected(void) {
    return 0;
}

uint32_t virtio_irq_count(void) {
    return 0;
}

/* No sockets on this rig (net.c links alone): the pump's demux
 * call lands here (unreached: this test never pumps). */
int sock_demux_frame(const uint8_t *frame, uint32_t len) {
    (void)frame;
    (void)len;
    return 0;
}

/* No IRQs on the host: the subsystem lock is a no-op (all pump
 * and queue paths execute single-threaded). */
void net_rx_lock(void) {
}

void net_rx_unlock(void) {
}

/* No sockets on this rig either (net.c links alone): the pool
 * lock init is a no-op for the same single-threaded reason. */
void sock_lock_init(void) {
}
/* Independent checksum oracle (different loop shape). */
static uint16_t oracle_ck(const uint8_t *d, uint32_t n) {
    uint32_t s = 0;
    uint32_t i = 0;
    while (i < n) {
        uint16_t w = (uint16_t)d[i] << 8;
        if (i + 1u < n) {
            w |= d[i + 1u];
        }
        s += w;
        s = (s & 0xFFFFu) + (s >> 16);
        i += 2u;
    }
    return (uint16_t)(~s & 0xFFFFu);
}

static uint32_t lcg_state = 0x9E7711u;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(int argc, char **argv) {
    uint8_t pkt[512];
    uint8_t tmp[512];
    /* Corpus dir from argv[1] (nova.py passes it absolute);
     * default is the in-tree relative path. */
    const char *cdir = (argc > 1) ? argv[1] : "tests/corpus";

    check(net_init() == 0, "init", 0, 0);

    /* Vectors incl. RFC 1071 pair (oracle-cross-checked below). */
    {
        static const uint8_t v1[8] = { 0x00, 0x01, 0xF2, 0x03,
                                       0xF4, 0xF5, 0xF6, 0xF7 };
        check(net_checksum(v1, 8) == 0x220D, "vec-even", 0, 0);
        check(net_checksum(v1, 7) == 0x2304, "vec-odd", 0, 0);
        check(net_checksum(v1, 0) == 0xFFFF, "vec-empty", 0, 0);
    }

    /* Oracle agreement on random buffers incl. odd lengths. */
    for (int t = 0; t < 2000; t++) {
        uint32_t n = lcg() % 64u;
        for (uint32_t i = 0; i < n; i++) {
            tmp[i] = (uint8_t)lcg();
        }
        if (net_checksum(tmp, n) != oracle_ck(tmp, n)) {
            check(0, "oracle", n, (unsigned long)t);
            break;
        }
    }
    printf("fuzz: 2000 checksum buffers vs oracle\n");

    /* IP build/parse round-trip + field fidelity. */
    {
        int hl = net_ip_build(pkt, 0x0A000001u, 0x0A000002u, 6, 100);
        uint8_t pr = 0;
        uint32_t s = 0, d = 0;
        int off;
        check(hl == 20, "ip-len", (unsigned long)hl, 0);
        off = net_ip_parse(pkt, 120, &pr, &s, &d);
        check(off == 20 && pr == 6 && s == 0x0A000001u &&
                  d == 0x0A000002u,
              "ip-rt", (unsigned long)off, pr);
        /* Truncated / bad version / bad checksum refuse. */
        check(net_ip_parse(pkt, 10, &pr, &s, &d) == -NOVA_EINVAL,
              "ip-short", 0, 0);
        pkt[0] = 0x65u;
        check(net_ip_parse(pkt, 120, &pr, &s, &d) == -NOVA_EINVAL,
              "ip-ver", 0, 0);
        pkt[0] = 0x45u;
        pkt[10] ^= 0xFFu;
        check(net_ip_parse(pkt, 120, &pr, &s, &d) == -NOVA_EINVAL,
              "ip-ck", 0, 0);
    }

    /* ICMP build/check incl. mismatch discipline (requests fail
     * the reply check until converted, mirroring the guest path). */
    {
        uint8_t pay[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
        int cl = net_icmp_build(pkt, 0xBEu, 0xEFu, pay, 8);
        check(cl == 16, "icmp-len", (unsigned long)cl, 0);
        check(net_icmp_check(pkt, 16, 0xBEu, 0xEFu) == -NOVA_EINVAL,
              "icmp-req", 0, 0);
        pkt[0] = 0;
        pkt[2] = 0;
        pkt[3] = 0;
        {
            uint16_t c = net_checksum(pkt, 16);
            pkt[2] = (uint8_t)(c >> 8);
            pkt[3] = (uint8_t)(c & 0xFFu);
        }
        check(net_icmp_check(pkt, 16, 0xBEu, 0xEFu) == 0, "icmp-ok",
              0, 0);
        check(net_icmp_check(pkt, 16, 0xBEu, 0xEEu) == -NOVA_EINVAL,
              "icmp-seq", 0, 0);
        pkt[0] = 8u;
        check(net_icmp_check(pkt, 16, 0xBEu, 0xEFu) == -NOVA_EINVAL,
              "icmp-type", 0, 0);
    }

    /* Loopback empty + over-MTU discipline. */
    {
        uint8_t big[NET_MTU + 1u];
        memset(big, 0xAA, sizeof(big));
        check(net_loop_rx(pkt, sizeof(pkt)) == -NOVA_EAGAIN,
              "loop-empty", 0, 0);
        check(net_loop_tx(big, sizeof(big)) == -NOVA_EINVAL,
              "loop-huge", 0, 0);
    }

    /* End-to-end ping incl. RTT + unroutable. */
    {
        int32_t r = net_ping(NET_ADDR_LOOPBACK, 42);
        check(r >= 0, "ping", (unsigned long)r, 0);
        check(net_ping(0xC0000201u, 1) == -NOVA_EHOSTUNREACH,
              "noroute", 0, 0);
    }

    /* UDP build pinned to the python oracle (68->67, 8B payload
     * 0..7 = 0xdb26) + parse discipline. */
    {
        uint8_t pay[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        uint16_t sp = 0, dp = 0;
        int ul = net_udp_build(pkt, 0x0A00020Fu, 0x0A000202u, 68,
                               67, pay, 8);
        check(ul == 16, "udp-len", (unsigned long)ul, 0);
        check(pkt[6] == 0xDB && pkt[7] == 0x26, "udp-pin", pkt[6],
              pkt[7]);
        check(net_udp_parse(pkt, 16, 0x0A00020Fu, 0x0A000202u, &sp,
                            &dp) == 8 && sp == 68 && dp == 67,
              "udp-rt", sp, dp);
        pkt[15] ^= 0xFFu;
        check(net_udp_parse(pkt, 16, 0x0A00020Fu, 0x0A000202u, &sp,
                            &dp) == -NOVA_EINVAL,
              "udp-mut", 0, 0);
        pkt[15] ^= 0xFFu;
        pkt[6] = 0;
        pkt[7] = 0;
        check(net_udp_parse(pkt, 16, 0x0A00020Fu, 0x0A000202u, &sp,
                            &dp) == 8,
              "udp-nock", 0, 0);
        pkt[4] = 0;
        pkt[5] = 24;
        check(net_udp_parse(pkt, 16, 0x0A00020Fu, 0x0A000202u, &sp,
                            &dp) == -NOVA_EINVAL,
              "udp-lenlie", 0, 0);
    }

    /* UDP checksum vs independent check: pseudo + packet (with
     * stored checksum) must fold to zero. */
    for (int t = 0; t < 500; t++) {
        uint32_t n = lcg() % 64u;
        uint32_t s = lcg();
        uint32_t d = lcg();
        uint16_t sp = (uint16_t)lcg();
        uint16_t dp = (uint16_t)lcg();
        int ul;
        uint8_t cat[1600];
        for (uint32_t i = 0; i < n; i++) {
            tmp[i] = (uint8_t)lcg();
        }
        ul = net_udp_build(pkt, s, d, sp, dp, tmp, n);
        check(ul == (int)(n + 8u), "udp-len2", (unsigned long)ul,
              n);
        if (ul < 0) {
            continue;
        }
        cat[0] = (uint8_t)(s >> 24);
        cat[1] = (uint8_t)(s >> 16);
        cat[2] = (uint8_t)(s >> 8);
        cat[3] = (uint8_t)s;
        cat[4] = (uint8_t)(d >> 24);
        cat[5] = (uint8_t)(d >> 16);
        cat[6] = (uint8_t)(d >> 8);
        cat[7] = (uint8_t)d;
        cat[8] = 0;
        cat[9] = 17;
        cat[10] = (uint8_t)((n + 8u) >> 8);
        cat[11] = (uint8_t)((n + 8u) & 0xFFu);
        memcpy(cat + 12, pkt, (size_t)(n + 8u));
        if (oracle_ck(cat, 12 + n + 8u) != 0) {
            check(0, "udp-oracle", n, (unsigned long)t);
            break;
        }
    }
    printf("fuzz: 500 udp checksums vs recomputation\n");

    /* IP completion: fragments + dead TTL refuse (DF passes). */
    {
        uint8_t pr = 0;
        uint32_t s = 0, d = 0;
        int hl = net_ip_build(pkt, 0x0A000001u, 0x0A000002u, 17,
                              8);
        check(hl == 20, "ipf-len", (unsigned long)hl, 0);
        check(net_ip_parse(pkt, 28, &pr, &s, &d) == 20, "ipf-df",
              0, 0);
        pkt[6] = 0x20u;
        pkt[10] = 0;
        pkt[11] = 0;
        {
            uint16_t c = net_checksum(pkt, 20);
            pkt[10] = (uint8_t)(c >> 8);
            pkt[11] = (uint8_t)(c & 0xFFu);
        }
        check(net_ip_parse(pkt, 28, &pr, &s, &d) == -NOVA_EINVAL,
              "ipf-mf", 0, 0);
        pkt[6] = 0x40u;
        pkt[7] = 0x01u;
        pkt[10] = 0;
        pkt[11] = 0;
        {
            uint16_t c = net_checksum(pkt, 20);
            pkt[10] = (uint8_t)(c >> 8);
            pkt[11] = (uint8_t)(c & 0xFFu);
        }
        check(net_ip_parse(pkt, 28, &pr, &s, &d) == -NOVA_EINVAL,
              "ipf-off", 0, 0);
        pkt[7] = 0x00u;
        pkt[8] = 0;
        pkt[10] = 0;
        pkt[11] = 0;
        {
            uint16_t c = net_checksum(pkt, 20);
            pkt[10] = (uint8_t)(c >> 8);
            pkt[11] = (uint8_t)(c & 0xFFu);
        }
        check(net_ip_parse(pkt, 28, &pr, &s, &d) == -NOVA_EINVAL,
              "ipf-ttl", 0, 0);
    }

    /* DHCP validator: crafted OFFER + rogue mutants. */
    {
        static const uint8_t tmac[6] = { 0x08, 0x00, 0x27,
                                         0x3C, 0xA1, 0xA1 };
        uint8_t msg[300];
        uint8_t mt = 0;
        uint32_t y = 0, sv = 0, mk = 0, rt = 0, le = 0;
        uint32_t p = 240u;
        memset(msg, 0, sizeof(msg));
        msg[0] = 2;
        msg[1] = 1;
        msg[2] = 6;
        msg[4] = 0x12;
        msg[5] = 0x34;
        msg[6] = 0x56;
        msg[7] = 0x78;
        memcpy(msg + 28, tmac, 6);
        msg[16] = 10;
        msg[18] = 2;
        msg[19] = 15;
        msg[236] = 99;
        msg[237] = 130;
        msg[238] = 83;
        msg[239] = 99;
        msg[p++] = 53;
        msg[p++] = 1;
        msg[p++] = 2;
        msg[p++] = 54;
        msg[p++] = 4;
        msg[p++] = 10;
        msg[p++] = 0;
        msg[p++] = 2;
        msg[p++] = 2;
        msg[p++] = 1;
        msg[p++] = 4;
        msg[p++] = 255;
        msg[p++] = 255;
        msg[p++] = 255;
        msg[p++] = 0;
        msg[p++] = 3;
        msg[p++] = 4;
        msg[p++] = 10;
        msg[p++] = 0;
        msg[p++] = 2;
        msg[p++] = 2;
        msg[p++] = 51;
        msg[p++] = 4;
        msg[p++] = 0;
        msg[p++] = 0;
        msg[p++] = 0x0E;
        msg[p++] = 0x10;
        msg[p++] = 255;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == 0 &&
                  mt == 2 && y == 0x0A00020Fu &&
                  sv == 0x0A000202u && mk == 0xFFFFFF00u &&
                  rt == 0x0A000202u && le == 3600u,
              "dhcp-offer", mt, le);
        msg[4] ^= 1u;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == -NOVA_EINVAL,
              "dhcp-xid", 0, 0);
        msg[4] ^= 1u;
        msg[236] = 0;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == -NOVA_EINVAL,
              "dhcp-magic", 0, 0);
        msg[236] = 99;
        check(dhcp_parse_msg(msg, 200, 0x12345678u, tmac, &mt, &y,
                             &sv, &mk, &rt, &le) == -NOVA_EINVAL,
              "dhcp-short", 0, 0);
        msg[28] ^= 1u;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == -NOVA_EINVAL,
              "dhcp-chaddr", 0, 0);
        msg[28] ^= 1u;
        msg[19] = 0;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == -NOVA_EINVAL,
              "dhcp-wireaddr", 0, 0);
        msg[19] = 15;
        /* NAK: explicit refusal succeeds with nothing trusted
         * (outs untouched: still zero from the memset above...
         * reuse the zeroed tail by re-zeroing first). */
        memset(msg, 0, sizeof(msg));
        msg[0] = 2;
        msg[1] = 1;
        msg[2] = 6;
        msg[4] = 0x12;
        msg[5] = 0x34;
        msg[6] = 0x56;
        msg[7] = 0x78;
        memcpy(msg + 28, tmac, 6);
        msg[236] = 99;
        msg[237] = 130;
        msg[238] = 83;
        msg[239] = 99;
        msg[240] = 53;
        msg[241] = 1;
        msg[242] = 6;
        msg[243] = 255;
        mt = 0xFFu;
        y = 0xDEADu;
        check(dhcp_parse_msg(msg, sizeof(msg), 0x12345678u, tmac,
                             &mt, &y, &sv, &mk, &rt,
                             &le) == 0 && mt == 6u &&
                   y == 0xDEADu,
              "dhcp-nak", mt, y);
    }

    /* No NIC on the host: acquisition refuses cleanly. */
    check(dhcp_acquire() == -NOVA_ENODEV, "dhcp-nonic", 0, 0);

    /* DNS codec (Phase 14b): pinned query + reply matrix. Host
     * runs the pure codec only (no wire without a NIC). */
    {
        static const uint8_t expect[29] = {
            0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x07, 'e', 'x', 'a',
            'm', 'p', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00,
            0x00, 0x01, 0x00, 0x01
        };
        uint8_t q[64];
        uint8_t r[128];
        uint32_t i;
        int ql = dns_build(q, sizeof(q), 0x1234, "example.com");
        uint32_t rl;
        uint32_t ip = 0;
        static const uint8_t ans[16] = {
            0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x3C, 0x00, 0x04,
            93, 184, 216, 34
        };
        check(ql == 29, "dns-len", (unsigned long)ql, 0);
        for (i = 0; ql == 29 && i < 29u; i++) {
            if (q[i] != expect[i]) {
                check(0, "dns-bytes", i, q[i]);
                break;
            }
        }
        check(dns_build(q, 10, 0x1234, "example.com") ==
                  -NOVA_EINVAL,
              "dns-small", 0, 0);
        check(dns_build(q, sizeof(q), 0x1234, "") == -NOVA_EINVAL,
              "dns-empty", 0, 0);
        check(dns_build(q, sizeof(q), 0x1234, "bad_name") ==
                  -NOVA_EINVAL,
              "dns-chars", 0, 0);
        /* Crash-guard: a broken encoder must FAIL checks below,
         * not walk off the stack (mirrors guest early-return). */
        if (ql != 29) {
            ql = 0;
        }
        /* Reply skeleton around the built question. */
        r[0] = 0x12;
        r[1] = 0x34;
        r[2] = 0x81;
        r[3] = 0x80;
        r[4] = 0;
        r[5] = 1;
        r[6] = 0;
        r[7] = 1;
        r[8] = 0;
        r[9] = 0;
        r[10] = 0;
        r[11] = 0;
        for (i = 0; i + 12u < (uint32_t)ql; i++) {
            r[12u + i] = q[12u + i];
        }
        rl = (uint32_t)ql;
        for (i = 0; i < 16u; i++) {
            r[rl + i] = ans[i];
        }
        rl += 16u;
        check(dns_parse(r, rl, 0x1234, &ip) == 0 &&
                  ip == 0x5DB8D822u,
              "dns-ok", ip, 0);
        r[0] ^= 1u;
        check(dns_parse(r, rl, 0x1234, &ip) == -NOVA_EINVAL,
              "dns-id", 0, 0);
        r[0] ^= 1u;
        r[3] = 0x83u;
        check(dns_parse(r, rl, 0x1234, &ip) == -NOVA_ENOENT,
              "dns-nx", 0, 0);
        r[3] = 0x80u;
        if (dns_parse(r, rl - 5u, 0x1234, &ip) != -NOVA_EINVAL) {
            check(0, "dns-trunc", 0, 0);
        }
        r[ql] = 0xC0;
        r[ql + 1u] = 0x0C;
        r[ql + 2u] = 0x00;
        r[ql + 3u] = 0x05;
        check(dns_parse(r, rl, 0x1234, &ip) == -NOVA_ENOENT,
              "dns-cname", 0, 0);
    }

    /* Checked-in corpus replay (tests/corpus seed files, built by
     * tools/mkcorpus.py from the same constructions as the
     * inline mutants above): every file replays through the
     * real parser with its recorded expectation. A drifted or
     * corrupted file fails loudly (regenerate, don't delete). */
    {
        static const uint8_t tmac2[6] = { 0x08, 0x00, 0x27,
                                          0x3C, 0xA1, 0xA1 };
        static const struct {
            const char *name;
            int is_dns;
            int expect;
        } items[] = {
            { "dhcp/dhcp-offer.bin", 0, 0 },
            { "dhcp/dhcp-xid.bin", 0, -NOVA_EINVAL },
            { "dhcp/dhcp-magic.bin", 0, -NOVA_EINVAL },
            { "dhcp/dhcp-short.bin", 0, -NOVA_EINVAL },
            { "dhcp/dhcp-chaddr.bin", 0, -NOVA_EINVAL },
            { "dhcp/dhcp-wireaddr.bin", 0, -NOVA_EINVAL },
            { "dhcp/dhcp-noend.bin", 0, 0 },
            { "dhcp/dhcp-badlen.bin", 0, -NOVA_EINVAL },
            { "dns/dns-ok.bin", 1, 0 },
            { "dns/dns-id.bin", 1, -NOVA_EINVAL },
            { "dns/dns-nxdomain.bin", 1, -NOVA_ENOENT },
            { "dns/dns-trunc.bin", 1, -NOVA_EINVAL },
            { "dns/dns-cname.bin", 1, -NOVA_ENOENT },
            { "dns/dns-ptrloop.bin", 1, -NOVA_EINVAL },
            { "dns/dns-2ans.bin", 1, 0 },
        };
        char path[256];
        uint32_t i;
        for (i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
            FILE *f;
            uint8_t buf[320];
            size_t n = 0;
            size_t dl = strlen(cdir);
            size_t nl = strlen(items[i].name);
            uint8_t mt = 0;
            uint32_t y = 0, sv = 0, mk = 0, rt = 0, le = 0;
            uint32_t ip = 0;
            int rc;
            if (dl + nl + 2u > sizeof(path)) {
                check(0, "corpus-path", i, 0);
                continue;
            }
            memcpy(path, cdir, dl);
            path[dl] = '/';
            memcpy(path + dl + 1u, items[i].name, nl);
            path[dl + 1u + nl] = 0;
            f = fopen(path, "rb");
            if (f == 0) {
                check(0, "corpus-open", i, 0);
                continue;
            }
            n = fread(buf, 1, sizeof(buf), f);
            fclose(f);
            if (items[i].is_dns) {
                rc = dns_parse(buf, (uint32_t)n, 0x1234, &ip);
                check(rc == items[i].expect, "corpus-dns",
                      (unsigned long)i, (unsigned long)rc);
                if (items[i].expect == 0) {
                    check(ip == 0x5DB8D822u, "corpus-dns-ip",
                          ip, 0);
                }
            } else {
                rc = dhcp_parse_msg(buf, (uint32_t)n,
                                    0x12345678u, tmac2, &mt, &y,
                                    &sv, &mk, &rt, &le);
                check(rc == items[i].expect, "corpus-dhcp",
                      (unsigned long)i, (unsigned long)rc);
                if (items[i].expect == 0) {
                    check(mt == 2 && y == 0x0A00020Fu &&
                              sv == 0x0A000202u &&
                              mk == 0xFFFFFF00u &&
                              rt == 0x0A000202u && le == 3600u,
                          "corpus-dhcp-fields", mt, le);
                }
            }
        }
        printf("corpus: 15 wire seeds replayed\n");
    }

    /* Guest selftest runs the same stack in-guest (NET-OK). */
    check(net_selftest() == 0, "selftest", 0, 0);

    if (g_failed) {
        printf("VERDICT: net BROKEN\n");
        return 1;
    }
    printf("VERDICT: net correct\n");
    return 0;
}
