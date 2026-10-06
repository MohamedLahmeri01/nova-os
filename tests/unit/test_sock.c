/* NOVA OS socket host test (Phase 14a, T21).
 * Host-side: the REAL kernel/net/sock.c; loopback never touches
 * virtio (net_* stubbed, never called on loopback paths), ticks
 * frozen (timeouts are immediate). No scheduler on host
 * (can_sleep is 0: the early-boot spin path executes, which is
 * exactly what the guest pre-scheduler selftest proves too).
 * The blocking proof needs real threads + timer: guest-only
 * (kernel/net/sock_block.c, SOCKBLK-OK).
 * Bind matrix, loopback round-trips, error discipline,
 * close/reuse, randomized open/send/recv/close fuzz for
 * crash-freedom. VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "net/sock.h"
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

uint32_t net_self_ip(void) {
    return 0x0A00020Fu;
}

int net_udp_send(uint32_t src, uint32_t dst, uint16_t sport,
                 uint16_t dport, const uint8_t *payload,
                 uint32_t len) {
    (void)src;
    (void)dst;
    (void)sport;
    (void)dport;
    (void)payload;
    (void)len;
    return -NOVA_EIO;
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

    /* Off-loopback would wire out (stub refuses: unreached). */
    check(sock_send(a, 0x0A000202u, 67, hello, 5) == -NOVA_EIO,
          "wire-stub", 0, 0);

    /* Close frees; ids and ports recycle. */
    check(sock_close(a) == 0 && sock_close(b) == 0, "close", 0,
          0);
    check(sock_open(5000) >= 0, "rebind", 0, 0);

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
