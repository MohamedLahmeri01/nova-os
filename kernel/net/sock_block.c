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
