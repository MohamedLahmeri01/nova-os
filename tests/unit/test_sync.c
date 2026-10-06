/* NOVA OS spinlock host test (Phase 0.3-E, T22).
 * Host-side: the REAL kernel/sync/sync.c. Contract proofs that
 * need no threads: init state, lock/unlock, trylock refused
 * while held, owner tracking, atomic op vectors (xchg/cmpxchg/
 * add). irqsave variants never execute here (cli is privileged
 * on host); thread/scheduler/process symbols are unreached
 * stubs (the contention proof is guest-only, SYNC-OK).
 * VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "sync/sync.h"
#include "thread/thread.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "arch/x86_64/cpu.h"
#else
#error "test_sync: no arch backend for this architecture"
#endif

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* --- kernel dep stubs (unreached: contention test is guest-only) --- */

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

struct process *g_init_process = 0;

struct thread *thread_create(struct process *p, thread_fn_t fn,
                             void *arg, uint8_t prio) {
    (void)p;
    (void)fn;
    (void)arg;
    (void)prio;
    return 0;
}

__attribute__((noreturn)) void thread_exit(int code) {
    (void)code;
    exit(2);
}

int main(void) {
    static struct spinlock lk;
    volatile uint32_t cell = 0;

    /* Atomics first (the lock stands on these). */
    check(arch_atomic_xchg_u32(&cell, 5u) == 0u && cell == 5u,
          "xchg", cell, 0);
    check(arch_atomic_cmpxchg_u32(&cell, 5u, 9u) == 5u &&
              cell == 9u,
          "cmpxchg-hit", cell, 0);
    check(arch_atomic_cmpxchg_u32(&cell, 5u, 7u) == 9u &&
              cell == 9u,
          "cmpxchg-miss", cell, 0);
    check(arch_atomic_add_u32(&cell, 3u) == 9u && cell == 12u,
          "add", cell, 0);

    /* Lock contract: init free, lock takes, trylock refuses
     * while held, unlock frees, owner tracks the holder. */
    spin_init(&lk, "t22");
    check(lk.locked == 0u && lk.owner == 0u, "init", lk.locked,
          0);
    spin_lock(&lk, (uintptr_t)0xA1u);
    check(lk.locked == 1u && lk.owner == (uintptr_t)0xA1u,
          "locked", lk.locked, 0);
    check(spin_trylock(&lk, (uintptr_t)0xB2u) == -NOVA_EBUSY,
          "try-held", 0, 0);
    check(lk.owner == (uintptr_t)0xA1u, "owner-kept",
          (unsigned long)lk.owner, 0);
    spin_unlock(&lk);
    check(lk.locked == 0u && lk.owner == 0u, "unlocked",
          lk.locked, 0);
    check(spin_trylock(&lk, (uintptr_t)0xB2u) == 0, "try-free",
          0, 0);
    check(lk.owner == (uintptr_t)0xB2u, "owner-set",
          (unsigned long)lk.owner, 0);
    spin_unlock(&lk);

    if (g_failed) {
        printf("VERDICT: sync BROKEN\n");
        return 1;
    }
    printf("VERDICT: sync correct\n");
    return 0;
}
