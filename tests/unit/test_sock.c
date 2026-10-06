/* NOVA OS socket host test (Phase 14a, T21; demux T22-vectors).
 * Host-side: the REAL kernel/net/sock.c + kernel/net/net.c
 * (builders/parsers real; virtio stubbed dormant, ticks frozen).
 * Bind matrix, loopback round-trips, wire-demux frame matrix,
 * error discipline, close/reuse, randomized fuzz. VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "net/sock.h"
#include "net/net.h"
#include "thread/thread.h"
#include "syscall/syscall.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* --- kernel dep stubs (loopback paths never reach them) --- */

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

/* No scheduler on host: recv takes the early-boot spin path
 * (frozen ticks: timeouts decide on the first check). */
int sched_can_sleep(void) {
    return 0;
}

int sched_sleep_until(uint32_t deadline) {
    (void)deadline;
    return 1;
}

void sched_wakeup(struct thread *t) {
    (void)t;
}

/* Unreached on host (can_sleep is 0, so the blocking path that
 * reads the current thread never executes); present for the
 * link, like the sleep/wake stubs above. */
struct thread *sched_current_thread(void) {
    return 0;
}

/* NIC claimed present (TX/RX still stubbed): the legacy
 * filtered recv gates on ready, while TX refuses at the
 * doorbell and RX feeds only the programmed frames. */
int virtio_net_ready(void) {
    return 1;
}

/* Fixed test MAC (the demux reads it as "ours"). */
static const uint8_t t_mac_self[6] = { 0x52, 0x54, 0x00,
                                       0x12, 0x34, 0x56 };
static const uint8_t t_mac_src[6] = { 0x52, 0x54, 0x00,
                                      0x12, 0x34, 0x57 };

const uint8_t *virtio_net_mac(void) {
    return t_mac_self;
}

int virtio_net_tx(const uint8_t *f, uint32_t l) {
    (void)f;
    (void)l;
    return -NOVA_EIO;
}

/* Programmable ring: queued frames drain in order, then
 * silence (lets the pump test drive ring -> slots + queue). */
static const uint8_t *rx_feed[8];
static uint32_t rx_feed_len[8];
static int rx_feed_n;
static int rx_feed_i;

int virtio_net_rx(uint8_t *o, uint32_t m) {
    uint32_t n;
    if (rx_feed_i >= rx_feed_n) {
        return -NOVA_EAGAIN;
    }
    n = rx_feed_len[rx_feed_i];
    if (n > m) {
        return -NOVA_ENOSPC;
    }
    memcpy(o, rx_feed[rx_feed_i], n);
    rx_feed_i++;
    return (int)n;
}

int virtio_irq_expected(void) {
    return 0;
}

uint32_t virtio_irq_count(void) {
    return 0;
}

/* No IRQs on the host: subsystem lock no-ops (pump + queue
 * run single-threaded here). */
void net_rx_lock(void) {
}

void net_rx_unlock(void) {
}

/* Build a full eth/IP/UDP frame (dst/src MAC + 0800, IP + UDP
 * via the real builders, so checksums validate). Returns the
 * total length. */
static uint32_t build_frame(uint8_t *f, const uint8_t *dmac,
                            uint32_t srcip, uint32_t dstip,
                            uint16_t sport, uint16_t dport,
                            const uint8_t *pl, uint32_t plen) {
    uint8_t ip[20];
    uint8_t u[72];
    int ulen;
    int iplen;
    int i;
    ulen = net_udp_build(u, srcip, dstip, sport, dport, pl,
                         plen);
    iplen = net_ip_build(ip, srcip, dstip, 17u, (uint32_t)ulen);
    for (i = 0; i < 6; i++) {
        f[i] = dmac[i];
        f[6 + i] = t_mac_src[i];
    }
    f[12] = 0x08;
    f[13] = 0x00;
    memcpy(f + 14, ip, 20);
    memcpy(f + 34, u, (uint32_t)ulen);
    (void)iplen;
    return 34u + (uint32_t)ulen;
}

static uint32_t lcg_state = 0x50C4E7u;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(void) {
    uint8_t rx[64];
    uint32_t got = 0;
    static const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
    int a, b;

    /* Guest selftest first (clean-pool expectations; drains
     * fully; never blocks so frozen ticks are fine). */
    check(sock_selftest() == 0, "selftest", 0, 0);

    /* Bind matrix. */
    a = sock_open(5000);
    check(a >= 0, "open", (unsigned long)a, 0);
    check(sock_open(5000) == -NOVA_EADDRINUSE, "dup", 0, 0);
    b = sock_open(0);
    check(b >= 0 && b != a, "ephem", (unsigned long)b, 0);
    check(sock_close(99) == -NOVA_EBADF, "close-bad", 0, 0);

    /* Loopback round-trip (no wire involved). */
    check(sock_send(a, 0x7F000001u, 5000, hello, 5) == 0,
          "send", 0, 0);
    check(sock_recv(a, rx, sizeof(rx), 10, &got) == 5 &&
              got == 5u && memcmp(rx, hello, 5) == 0,
          "recv", got, 0);
    check(sock_recv(a, rx, sizeof(rx), 0, &got) == -NOVA_EAGAIN,
          "empty", 0, 0);

    /* Self-IP routes loopback too; oversize refuses. */
    check(sock_send(a, 0x0A00020Fu, 5000, hello, 5) == 0,
          "send-self", 0, 0);
    check(sock_recv(a, rx, sizeof(rx), 10, &got) == 5, "recv-self",
          got, 0);
    check(sock_send(a, 0x7F000001u, 5000, hello,
                    SOCK_MTU_PAYLOAD + 1u) == -NOVA_EINVAL,
          "huge", 0, 0);

    /* Off-loopback reaches the real TX path, which fails at
     * the stub doorbell (ARP request never leaves). */
    check(sock_send(a, 0x0A000202u, 67, hello, 5) == -NOVA_EIO,
          "wire-nonic", 0, 0);

    /* Close frees; ids and ports recycle. */
    check(sock_close(a) == 0 && sock_close(b) == 0, "close", 0,
          0);
    check(sock_open(5000) >= 0, "rebind", 0, 0);

    /* Wire demux: crafted eth/IP/UDP frames through the real
     * parsers (ports 5200+, clear of the fuzz range). */
    {
        uint8_t f[64];
        uint8_t big[1600];
        uint8_t u[8 + 1473];
        uint8_t ip[20];
        static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF,
                                          0xFF, 0xFF, 0xFF };
        static const uint8_t other[6] = { 0x52, 0x54, 0x00,
                                         0x12, 0x34, 0x58 };
        uint32_t fl;
        int c = sock_open(5200);
        int i;
        check(c >= 0, "dmx-open", (unsigned long)c, 0);
        /* Exact hit: delivered, readable via the fast path. */
        fl = build_frame(f, t_mac_self, 0x0A000202u, 0x0A00020Fu,
                         67, 5200, hello, 5);
        check(sock_demux_frame(f, fl) == 1, "dmx-hit", 0, 0);
        check(sock_recv(c, rx, sizeof(rx), 10, &got) == 5 &&
                  got == 5u && memcmp(rx, hello, 5) == 0,
              "dmx-recv", got, 0);
        /* Wrong port: dropped, socket stays empty. */
        fl = build_frame(f, t_mac_self, 0x0A000202u, 0x0A00020Fu,
                         67, 5201, hello, 5);
        check(sock_demux_frame(f, fl) == 0, "dmx-port", 0, 0);
        check(sock_recv(c, rx, sizeof(rx), 0, &got) ==
                  -NOVA_EAGAIN,
              "dmx-empty", 0, 0);
        /* Broadcast MAC + self IP: delivered. */
        fl = build_frame(f, bcast, 0x0A000202u, 0x0A00020Fu,
                         67, 5200, hello, 5);
        check(sock_demux_frame(f, fl) == 1, "dmx-bcast", 0,
              0);
        check(sock_recv(c, rx, sizeof(rx), 10, &got) == 5,
              "dmx-bcast-recv", got, 0);
        /* Non-IP ethertype: dropped. */
        fl = build_frame(f, t_mac_self, 0x0A000202u, 0x0A00020Fu,
                         67, 5200, hello, 5);
        f[13] = 0x06;
        check(sock_demux_frame(f, fl) == 0, "dmx-arp", 0, 0);
        /* Foreign MAC / foreign IP: dropped. */
        fl = build_frame(f, other, 0x0A000202u, 0x0A00020Fu,
                         67, 5200, hello, 5);
        check(sock_demux_frame(f, fl) == 0, "dmx-mac", 0, 0);
        fl = build_frame(f, t_mac_self, 0x0A000202u, 0x0A000203u,
                         67, 5200, hello, 5);
        check(sock_demux_frame(f, fl) == 0, "dmx-ip", 0, 0);
        /* Short / NULL: malformed. */
        check(sock_demux_frame(f, 10) == -NOVA_EINVAL,
              "dmx-short", 0, 0);
        check(sock_demux_frame(0, 47) == -NOVA_EINVAL,
              "dmx-null", 0, 0);
        /* Oversize datagram (1473B payload, checksum absent =
         * accepted per RFC 768, but no slot fits): dropped,
         * never truncated. The IP header is hand-built: the
         * builder MTU-caps, but a jumbo parses fine. */
        for (i = 0; i < 8; i++) {
            u[i] = 0;
        }
        u[0] = 0;
        u[1] = 67;
        u[2] = 0x14;
        u[3] = 0x50;
        u[4] = 0x05;
        u[5] = 0xC9; /* ulen 1481 */
        u[6] = 0;
        u[7] = 0; /* checksum absent */
        for (i = 0; i < 1473; i++) {
            u[8 + i] = 0x41;
        }
        {
            uint16_t ipc;
            ip[0] = 0x45;
            ip[1] = 0x00;
            ip[2] = 0x05;
            ip[3] = 0xDD; /* total 1501 */
            ip[4] = 0x12;
            ip[5] = 0x34;
            ip[6] = 0x40;
            ip[7] = 0x00; /* DF, offset 0 */
            ip[8] = 64;
            ip[9] = 17;
            ip[10] = 0;
            ip[11] = 0;
            ip[12] = 0x0A;
            ip[13] = 0x00;
            ip[14] = 0x02;
            ip[15] = 0x02; /* src 10.0.2.2 */
            ip[16] = 0x0A;
            ip[17] = 0x00;
            ip[18] = 0x02;
            ip[19] = 0x0F; /* dst 10.0.2.15 */
            ipc = net_checksum(ip, 20);
            ip[10] = (uint8_t)(ipc >> 8);
            ip[11] = (uint8_t)(ipc & 0xFFu);
            for (i = 0; i < 6; i++) {
                big[i] = t_mac_self[i];
                big[6 + i] = t_mac_src[i];
            }
            big[12] = 0x08;
            big[13] = 0x00;
            memcpy(big + 14, ip, 20);
            memcpy(big + 34, u, 1481u);
        }
        check(sock_demux_frame(big, 34u + 1481u) == 0,
              "dmx-huge", 0, 0);
        check(sock_recv(c, rx, sizeof(rx), 0, &got) ==
                  -NOVA_EAGAIN,
              "dmx-huge-empty", 0, 0);
        /* Unbound after close: dropped. */
        check(sock_close(c) == 0, "dmx-close", 0, 0);
        fl = build_frame(f, t_mac_self, 0x0A000202u, 0x0A00020Fu,
                         67, 5200, hello, 5);
        check(sock_demux_frame(f, fl) == 0, "dmx-gone", 0, 0);
    }

    /* Ring pump: one drain routes the socket frame to its slot
     * (counted) and parks the other in the pending queue, where
     * the legacy filtered recv finds it. */
    {
        static uint8_t fa[64];
        static uint8_t fb[64];
        static const uint8_t world[5] = { 'W', 'o', 'r', 'l',
                                          'd' };
        uint8_t uo[64];
        uint32_t fla, flb, ulen = 0;
        int p = sock_open(5200);
        check(p >= 0, "pump-open", (unsigned long)p, 0);
        fla = build_frame(fa, t_mac_self, 0x0A000202u,
                          0x0A00020Fu, 67, 5200, hello, 5);
        flb = build_frame(fb, t_mac_self, 0x0A000202u,
                          0x0A00020Fu, 4000, 67, world, 5);
        rx_feed[0] = fa;
        rx_feed_len[0] = fla;
        rx_feed[1] = fb;
        rx_feed_len[1] = flb;
        rx_feed_n = 2;
        rx_feed_i = 0;
        check(net_rx_pump() == 1, "pump-divert", 0, 0);
        check(net_rx_pump() == 0, "pump-drained", 0, 0);
        check(sock_recv(p, rx, sizeof(rx), 10, &got) == 5 &&
                  got == 5u && memcmp(rx, hello, 5) == 0,
              "pump-slot", got, 0);
        /* Queued frame: legacy filtered recv reads it back
         * (frozen ticks are safe: the queue hits first try). */
        check(net_udp_recv(uo, sizeof(uo), 0, 67, 0x0A00020Fu,
                           100, &ulen) == 0 && ulen == 5u &&
                  memcmp(uo, world, 5) == 0,
              "pump-queue", ulen, 0);
        check(sock_close(p) == 0, "pump-close", 0, 0);
    }

    /* Randomized open/send/recv/close fuzz (crash-freedom +
     * errno discipline; frozen ticks make recv instant). */
    for (int t = 0; t < 2000; t++) {
        int op = (int)(lcg() % 4u);
        int id = (int)(lcg() % 10u);
        uint8_t pb[8];
        uint32_t gl = 0;
        uint8_t rb[16];
        for (uint32_t i = 0; i < sizeof(pb); i++) {
            pb[i] = (uint8_t)lcg();
        }
        if (op == 0) {
            int r = sock_open((uint16_t)(4000u + lcg() % 20u));
            check(r >= 0 || r == -NOVA_EADDRINUSE ||
                      r == -NOVA_EMFILE,
                  "fuzz-open", (unsigned long)r, 0);
        } else if (op == 1) {
            int r = sock_close((uint32_t)id);
            check(r == 0 || r == -NOVA_EBADF, "fuzz-close",
                  (unsigned long)r, 0);
        } else if (op == 2) {
            int r = sock_send((uint32_t)id, 0x7F000001u,
                              (uint16_t)(4000u + lcg() % 20u),
                              pb, sizeof(pb));
            check(r == 0 || r == -NOVA_EBADF, "fuzz-send",
                  (unsigned long)r, 0);
        } else {
            int r = sock_recv((uint32_t)id, rb, sizeof(rb), 0,
                              &gl);
            check(r >= 0 || r == -NOVA_EBADF ||
                      r == -NOVA_EAGAIN || r == -NOVA_ENOSPC,
                  "fuzz-recv", (unsigned long)r, 0);
        }
    }
    printf("fuzz: 2000 socket ops vs discipline\n");

    if (g_failed) {
        printf("VERDICT: sockets BROKEN\n");
        return 1;
    }
    printf("VERDICT: sockets correct\n");
    return 0;
}
