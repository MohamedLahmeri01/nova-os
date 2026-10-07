/* NOVA OS blocking-socket proof (Phase 15-ii, guest only).
 * Never host-linked (needs real threads + timer): T21 covers
 * sock.c's spin path, the guest covers this file (SOCKBLK-OK).
 * Worker recvs empty (deadline +500) while main watches BLOCKED;
 * main's loopback send wakes it with 5 bytes. Second phase: empty
 * recv +30 with no sender must EAGAIN in-window. Close-while-
 * parked must EBADF, not hang.
 */
#include <stdint.h>

#include "serial.h"
#include "net/sock.h"
#include "net/net.h"
#include "drivers/virtio_net.h"
#include "sched/sched.h"
#include "thread/thread.h"
#include "process/process.h"
#include "syscall/syscall.h"
#include "time/time.h"

#define SOCK_LOOPBACK 0x7F000001u

static volatile int blk_done;
static volatile int blk_phase;
static volatile uint32_t blk_entry;
static volatile uint32_t blk_wake;
static volatile int blk_rcA;
static volatile int blk_rcB;
static volatile uint32_t blk_gotA;
static volatile uint32_t blk_gotB;
static uint8_t blk_rx[64];
static int blk_id;
static struct thread *blk_worker;
static const uint8_t blk_hello[5] = { 'h', 'e', 'l', 'l', 'o' };

static void block_worker(void *arg) {
    uint32_t got = 0;
    (void)arg;
    blk_entry = g_ticks;
    blk_rcA = sock_recv((uint32_t)blk_id, blk_rx, sizeof(blk_rx),
                        500, &got);
    blk_gotA = got;
    blk_phase = 1;
    blk_entry = g_ticks;
    got = 0;
    blk_rcB = sock_recv((uint32_t)blk_id, blk_rx, sizeof(blk_rx),
                        30, &got);
    blk_gotB = got;
    blk_wake = g_ticks;
    blk_phase = 2;
    blk_done = 1;
    thread_exit(0);
}

int sock_block_selftest(void) {
    int a, b, i;
    struct thread *w;
    uint32_t t0;
    uint32_t got = 0;
    blk_done = 0;
    blk_phase = 0;
    blk_rcA = -1;
    blk_rcB = -1;
    /* Pool is drained (sock_selftest ran at boot); scheduler +
     * timer live (runs after the sleep selftest). */
    a = sock_open(5100);
    b = sock_open(5101);
    if (a < 0 || b < 0) {
        serial_puts("SOCKBLK-FAIL open\n");
        return -1;
    }
    blk_id = a;
    w = thread_create(g_init_process, block_worker, 0, 16u);
    if (w == 0) {
        serial_puts("SOCKBLK-FAIL spawn\n");
        return -1;
    }
    blk_worker = w;
    sched_yield(); /* worker runs to its parked recv */
    /* Phase A: worker truly blocked (not fast-pathed); the
     * loopback send wakes it with the bytes. */
    t0 = g_ticks;
    while (blk_worker->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL park\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (blk_phase != 0) {
        serial_puts("SOCKBLK-FAIL phase\n");
        return -1;
    }
    if (sock_send((uint32_t)b, SOCK_LOOPBACK, 5100, blk_hello,
                  sizeof(blk_hello)) != 0) {
        serial_puts("SOCKBLK-FAIL send\n");
        return -1;
    }
    /* Phase B: drained again, parked with +30 and no sender;
     * the deadline must expire. */
    t0 = g_ticks;
    while (blk_phase != 1) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL phase1\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    t0 = g_ticks;
    while (blk_worker->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL park1\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    t0 = g_ticks;
    while (!blk_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL expire\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (blk_rcA != 5 || blk_gotA != 5u) {
        serial_puts("SOCKBLK-FAIL data\n");
        return -1;
    }
    for (i = 0; i < 5; i++) {
        if (blk_rx[i] != blk_hello[i]) {
            serial_puts("SOCKBLK-FAIL bytes\n");
            return -1;
        }
    }
    if (blk_rcB != -NOVA_EAGAIN) {
        serial_puts("SOCKBLK-FAIL eagain\n");
        return -1;
    }
    if (blk_wake - blk_entry < 30u || blk_wake - blk_entry > 500u) {
        serial_puts("SOCKBLK-FAIL window\n");
        return -1;
    }
    /* Close-while-parked fails the waiter (EBADF, not a hang):
     * park a fresh worker on the still-open socket, close under
     * it, expect EBADF out of phase A (phase B then hits the
     * closed id the same way). */
    blk_done = 0;
    blk_phase = 0;
    blk_rcA = -1;
    blk_rcB = -1;
    w = thread_create(g_init_process, block_worker, 0, 16u);
    if (w == 0) {
        serial_puts("SOCKBLK-FAIL spawn2\n");
        return -1;
    }
    blk_worker = w;
    sched_yield();
    t0 = g_ticks;
    while (blk_worker->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL park2\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (blk_phase != 0) {
        serial_puts("SOCKBLK-FAIL phase2\n");
        return -1;
    }
    if (sock_close((uint32_t)a) != 0) {
        serial_puts("SOCKBLK-FAIL close\n");
        return -1;
    }
    t0 = g_ticks;
    while (!blk_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SOCKBLK-FAIL done2\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (blk_rcA != -NOVA_EBADF || blk_rcB != -NOVA_EBADF) {
        serial_puts("SOCKBLK-FAIL ebadf\n");
        return -1;
    }
    if (sock_close((uint32_t)b) != 0) {
        serial_puts("SOCKBLK-FAIL close2\n");
        return -1;
    }
    /* Silence: the pool is drained, no waiter lingers. */
    if (sock_recv((uint32_t)b, blk_rx, sizeof(blk_rx), 0, &got) !=
        -NOVA_EBADF) {
        serial_puts("SOCKBLK-FAIL drain\n");
        return -1;
    }
    serial_puts("SOCKBLK-OK\n");
    return NOVA_ESUCCESS;
}

/* --- Phase I network stress (NETSTRESS-OK, guest only) ---
 * Single-threaded flood/churn/late-packet/rebind proofs plus
 * one same-priority concurrent pair on private sockets (exact
 * per-socket accounting; patterns differ to catch cross-talk).
 * All asserts structural/deterministic (no timing windows).
 * Pool lock (Phase I) serializes every shared mutation. */
static volatile uint32_t nst_adone, nst_bdone, nst_afail,
    nst_bfail;
static volatile uint32_t nst_gen_done, nst_gen_rc;

static void nst_worker(void *arg) {
    uint32_t side = (uint32_t)(uintptr_t)arg;
    uint32_t base = (side == 0) ? 5210u : 5220u;
    uint8_t tx[16];
    uint8_t rx[16];
    uint32_t got = 0;
    int a, b, i;
    for (i = 0; i < 16; i++) {
        tx[i] = (uint8_t)((side == 0) ? i : 0x80 + i);
    }
    a = sock_open((uint16_t)base);
    b = sock_open((uint16_t)(base + 1u));
    if (a < 0 || b < 0) {
        if (side == 0) {
            nst_afail = 1;
        } else {
            nst_bfail = 1;
        }
        goto done;
    }
    for (i = 0; i < 100; i++) {
        uint32_t k;
        if (sock_send((uint32_t)a, SOCK_LOOPBACK,
                      (uint16_t)(base + 1u), tx,
                      sizeof(tx)) != 0) {
            if (side == 0) {
                nst_afail = 1;
            } else {
                nst_bfail = 1;
            }
            break;
        }
        if (sock_recv((uint32_t)b, rx, sizeof(rx), 500,
                      &got) != 16 ||
            got != 16u) {
            if (side == 0) {
                nst_afail = 1;
            } else {
                nst_bfail = 1;
            }
            break;
        }
        for (k = 0; k < 16u; k++) {
            if (rx[k] != tx[k]) {
                if (side == 0) {
                    nst_afail = 1;
                } else {
                    nst_bfail = 1;
                }
                break;
            }
        }
        if ((side == 0 && nst_afail) ||
            (side != 0 && nst_bfail)) {
            break;
        }
    }
    sock_close((uint32_t)a);
    sock_close((uint32_t)b);
done:
    if (side == 0) {
        nst_adone = 1;
    } else {
        nst_bdone = 1;
    }
    thread_exit(0);
}

static void nst_gen_worker(void *arg) {
    uint32_t got = 0;
    uint8_t rx[16];
    (void)arg;
    /* Parked with a long deadline; main closes + reopens the
     * port under us. Expiry must report EBADF (generation
     * changed), never EAGAIN (slot looks live). */
    nst_gen_rc = (uint32_t)sock_recv((uint32_t)blk_id, rx,
                                     sizeof(rx), 500, &got);
    nst_gen_done = 1;
    thread_exit(0);
}

static volatile uint32_t nst_late_done, nst_late_rc1,
    nst_late_rc2, nst_late_got, nst_late_mid;
static uint8_t nst_late_rx[8];
static int nst_late_a, nst_late_b;

static void nst_late_worker(void *arg) {
    uint8_t rx[64];
    uint32_t got = 0;
    uint32_t k;
    (void)arg;
    /* Timeout on empty socket (main hlt-waits, never blocks:
     * parking main with nobody runnable panics sched-empty). */
    nst_late_rc1 =
        (uint32_t)sock_recv((uint32_t)nst_late_a, rx,
                            sizeof(rx), 30, &got);
    nst_late_mid = 1;
    /* Main sends between the two calls; this one must get it
     * (fast path if already delivered, wake path if parked). */
    got = 0;
    nst_late_rc2 =
        (uint32_t)sock_recv((uint32_t)nst_late_a, rx,
                            sizeof(rx), 500, &got);
    nst_late_got = got;
    for (k = 0; k < 8u; k++) {
        nst_late_rx[k] = rx[k];
    }
    nst_late_done = 1;
    thread_exit(0);
}

int net_stress_selftest(void) {
    int a, b, i;
    uint8_t rx[64];
    uint8_t tx[64];
    uint32_t got = 0;
    uint32_t t0;
    for (i = 0; i < 64; i++) {
        tx[i] = (uint8_t)i;
    }
    /* Flood: 300 loopback sends, single thread. Single-slot
     * overwrite keeps exactly the last; drain proves it. */
    a = sock_open(5200);
    b = sock_open(5201);
    if (a < 0 || b < 0) {
        serial_puts("NETSTRESS-FAIL open\n");
        return -1;
    }
    for (i = 0; i < 300; i++) {
        tx[0] = (uint8_t)i;
        if (sock_send((uint32_t)a, SOCK_LOOPBACK, 5200, tx,
                      sizeof(tx)) != 0) {
            serial_puts("NETSTRESS-FAIL flood-send\n");
            return -1;
        }
    }
    {
        uint32_t n = 0;
        for (;;) {
            int r = sock_recv((uint32_t)a, rx, sizeof(rx), 0,
                              &got);
            if (r == -NOVA_EAGAIN) {
                break;
            }
            if (r < 0) {
                serial_puts("NETSTRESS-FAIL flood-drain\n");
                return -1;
            }
            n++;
        }
        /* Sequential sends overwrite: exactly the last (299). */
        if (n != 1u || rx[0] != 299u % 256u) {
            serial_puts("NETSTRESS-FAIL flood-last\n");
            return -1;
        }
    }
    if (sock_close((uint32_t)a) != 0 ||
        sock_close((uint32_t)b) != 0) {
        serial_puts("NETSTRESS-FAIL flood-close\n");
        return -1;
    }
    /* Churn: 400 open/close waves over the whole pool plus
     * ephemeral picks; pool must drain clean every wave. */
    for (i = 0; i < 400; i++) {
        int ids[8];
        int k;
        for (k = 0; k < 8; k++) {
            ids[k] = sock_open((uint16_t)(6000u + k));
            if (ids[k] < 0) {
                serial_puts("NETSTRESS-FAIL churn-open\n");
                return -1;
            }
        }
        if (sock_open(7000) != -NOVA_EMFILE) {
            serial_puts("NETSTRESS-FAIL churn-full\n");
            return -1;
        }
        /* Pool full: ephemeral pick must also refuse. */
        if (sock_open(0) != -NOVA_EMFILE) {
            serial_puts("NETSTRESS-FAIL churn-ephem\n");
            return -1;
        }
        for (k = 0; k < 8; k++) {
            if (sock_close((uint32_t)ids[k]) != 0) {
                serial_puts("NETSTRESS-FAIL churn-close\n");
                return -1;
            }
        }
    }
    /* Timeout-then-late-packet: expiry leaves no residue.
     * A worker blocks (main must never park: nobody would
     * remain runnable). */
    a = sock_open(5200);
    b = sock_open(5201);
    if (a < 0 || b < 0) {
        serial_puts("NETSTRESS-FAIL late-open\n");
        return -1;
    }
    nst_late_a = a;
    nst_late_b = b;
    nst_late_done = 0;
    nst_late_mid = 0;
    if (thread_create(g_init_process, nst_late_worker, 0,
                      16u) == 0) {
        serial_puts("NETSTRESS-FAIL late-spawn\n");
        return -1;
    }
    t0 = g_ticks;
    while (!nst_late_mid) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("NETSTRESS-FAIL late-mid\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    for (i = 0; i < 5; i++) {
        tx[i] = (uint8_t)(0xC0 + i);
    }
    if (sock_send((uint32_t)nst_late_b, SOCK_LOOPBACK, 5200,
                  tx, 5) != 0) {
        serial_puts("NETSTRESS-FAIL late-send\n");
        return -1;
    }
    t0 = g_ticks;
    while (!nst_late_done) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("NETSTRESS-FAIL late-done\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if ((int32_t)nst_late_rc1 != -NOVA_EAGAIN ||
        (int32_t)nst_late_rc2 != 5 || nst_late_got != 5u ||
        nst_late_rx[0] != 0xC0) {
        serial_puts("NETSTRESS-FAIL late-data\n");
        return -1;
    }
    if (sock_close((uint32_t)a) != 0 ||
        sock_close((uint32_t)b) != 0) {
        serial_puts("NETSTRESS-FAIL close\n");
        return -1;
    }
    /* Rebind generation: parked waiter, close + reopen same
     * port under it (main never yields between the two, so
     * the waiter cannot interleave: deterministic gen path). */
    a = sock_open(5200);
    if (a < 0) {
        serial_puts("NETSTRESS-FAIL gen-open\n");
        return -1;
    }
    blk_id = a;
    nst_gen_done = 0;
    nst_gen_rc = 0;
    {
        struct thread *w =
            thread_create(g_init_process, nst_gen_worker, 0,
                          16u);
        if (w == 0) {
            serial_puts("NETSTRESS-FAIL gen-spawn\n");
            return -1;
        }
        sched_yield();
        t0 = g_ticks;
        while (w->state != THREAD_BLOCKED) {
            if (g_ticks - t0 > 2000u) {
                serial_puts("NETSTRESS-FAIL gen-park\n");
                return -1;
            }
            __asm__ volatile("hlt");
        }
        /* Still parked here (worker blocks till deadline or
         * wake; close wakes it, but the reopen below runs
         * before it can resume: straight-line, no yield). */
        if (sock_close((uint32_t)a) != 0) {
            serial_puts("NETSTRESS-FAIL gen-close\n");
            return -1;
        }
        b = sock_open(5200);
        if (b < 0) {
            serial_puts("NETSTRESS-FAIL gen-rebind\n");
            return -1;
        }
    }
    t0 = g_ticks;
    while (!nst_gen_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("NETSTRESS-FAIL gen-done\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    /* Slot live+used, generation bumped: only the gen check
     * can refuse (EBADF, not EAGAIN). */
    if ((int32_t)nst_gen_rc != -NOVA_EBADF) {
        serial_puts("NETSTRESS-FAIL gen-ebadf\n");
        return -1;
    }
    if (sock_close((uint32_t)b) != 0) {
        serial_puts("NETSTRESS-FAIL gen-close2\n");
        return -1;
    }
    /* Concurrent pair, private ports each: exact accounting,
     * patterns differ (cross-talk would corrupt bytes). */
    nst_adone = 0;
    nst_bdone = 0;
    nst_afail = 0;
    nst_bfail = 0;
    if (thread_create(g_init_process, nst_worker, (void *)0,
                      16u) == 0 ||
        thread_create(g_init_process, nst_worker, (void *)1,
                      16u) == 0) {
        serial_puts("NETSTRESS-FAIL pair-spawn\n");
        return -1;
    }
    t0 = g_ticks;
    while (!nst_adone || !nst_bdone) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("NETSTRESS-FAIL pair-join\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    if (nst_afail || nst_bfail) {
        serial_puts("NETSTRESS-FAIL pair-data\n");
        return -1;
    }
    /* Pool drains clean (all 8 reopenable: no leaked slot). */
    {
        int ids[8];
        int k;
        for (k = 0; k < 8; k++) {
            ids[k] = sock_open((uint16_t)(6000u + k));
            if (ids[k] < 0) {
                serial_puts("NETSTRESS-FAIL drain-open\n");
                return -1;
            }
        }
        for (k = 0; k < 8; k++) {
            sock_close((uint32_t)ids[k]);
        }
    }
    serial_puts("NETSTRESS-OK\n");
    return NOVA_ESUCCESS;
}

/* --- IRQ-path drill: crafted ring completion, real pump ---
 * After all wire flows (quiet ring, kernel CR3): bind 5200,
 * build a wire frame to it, inject a fake completion (the
 * drill runs the genuine lock + drain + demux path), expect
 * diverted == 1 with the bytes in the slot. Device progress
 * is restored, so the ring is exactly as before. */

int sock_drill_selftest(void) {
    static uint8_t frame[64];
    uint8_t ip[20];
    uint8_t u[16];
    const uint8_t *mymac;
    static const uint8_t smac[6] = { 0x52, 0x54, 0x00,
                                     0x12, 0x34, 0x57 };
    static const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
    int a, ulen, iplen, i;
    uint32_t got = 0;
    uint8_t rx[64];
    if (!virtio_net_ready()) {
        serial_puts("IRQDRV-SKIP\n");
        return NOVA_ESUCCESS; /* no NIC: nothing to drill */
    }
    mymac = virtio_net_mac();
    ulen = net_udp_build(u, NET_ADDR_GW, net_self_ip(), 67, 5200,
                         hello, sizeof(hello));
    iplen = net_ip_build(ip, NET_ADDR_GW, net_self_ip(),
                         NET_PROTO_UDP, (uint16_t)ulen);
    if (ulen != 13 || iplen != 20) {
        serial_puts("IRQDRV-FAIL build\n");
        return -1;
    }
    for (i = 0; i < 6; i++) {
        frame[i] = mymac[i];
        frame[6 + i] = smac[i];
    }
    frame[12] = 0x08;
    frame[13] = 0x00;
    for (i = 0; i < 20; i++) {
        frame[14 + i] = ip[i];
    }
    for (i = 0; i < 13; i++) {
        frame[34 + i] = u[i];
    }
    a = sock_open(5200);
    if (a < 0) {
        serial_puts("IRQDRV-FAIL open\n");
        return -1;
    }
    {
        int dr = virtio_net_drill(frame, 47u);
        if (dr != 1) {
            serial_puts("IRQDRV-FAIL divert rc=");
            serial_putdec32((uint32_t)dr);
            serial_putc('\n');
            return -1;
        }
    }
    if (sock_recv((uint32_t)a, rx, sizeof(rx), 0, &got) != 5 ||
        got != 5u) {
        serial_puts("IRQDRV-FAIL recv\n");
        return -1;
    }
    for (i = 0; i < 5; i++) {
        if (rx[i] != hello[i]) {
            serial_puts("IRQDRV-FAIL bytes\n");
            return -1;
        }
    }
    if (sock_close((uint32_t)a) != 0) {
        serial_puts("IRQDRV-FAIL close\n");
        return -1;
    }
    serial_puts("IRQPUMP-OK\n");
    return NOVA_ESUCCESS;
}
