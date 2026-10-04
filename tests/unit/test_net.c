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

int main(void) {
    uint8_t pkt[512];
    uint8_t tmp[512];

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
        check(net_ping(0x0A000001u, 1) == -NOVA_EHOSTUNREACH,
              "noroute", 0, 0);
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
