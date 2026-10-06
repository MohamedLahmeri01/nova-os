/* NOVA OS spinlock implementation (Phase 0.3-E).
 * xchg test-and-set (implicitly locked on x86) + pause spin.
 * No dependencies beyond arch atomics + headers: host-linkable,
 * so the contract proofs run on host (T22) and contention runs
 * in guest (selftest below).
 */
#include <stdint.h>

#include "sync/sync.h"
#include "syscall/syscall.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "serial.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "sync: no arch backend for this architecture"
#endif

void spin_init(struct spinlock *l, const char *name) {
    if (l == 0) {
        return;
    }
    l->locked = 0;
    l->name = name;
    l->owner = 0;
}

void spin_lock(struct spinlock *l, uintptr_t owner) {
    while (arch_atomic_xchg_u32(&l->locked, 1u) != 0) {
        arch_pause();
    }
    l->owner = owner;
}

int spin_trylock(struct spinlock *l, uintptr_t owner) {
    if (arch_atomic_xchg_u32(&l->locked, 1u) != 0) {
        return -NOVA_EBUSY;
    }
    l->owner = owner;
    return 0;
}

void spin_unlock(struct spinlock *l) {
    l->owner = 0;
    /* Release order: owner clears first so a concurrent trylock
     * sees held-with-owner or free-without — never
     * free-with-stale-owner. Single-core ordering suffices
     * today; SMP review owns the fence. */
    arch_atomic_xchg_u32(&l->locked, 0u);
}

void spin_lock_irqsave(struct spinlock *l, uintptr_t owner,
                       uint32_t *was_on) {
    uint32_t f = arch_irq_save();
    if (was_on != 0) {
        *was_on = f;
    }
    spin_lock(l, owner);
}

void spin_unlock_irqrestore(struct spinlock *l, uint32_t was_on) {
    spin_unlock(l);
    arch_irq_restore(was_on);
}

/* --- contention proof (scheduler live): exclusion under fire ---
 * Two threads hammer one lock-protected counter (100k each,
 * irqsave discipline: also proves IF save/restore is sound
 * and exclusion holds across preemption). Exact total or
 * failure. Prints SYNC-OK. */

static volatile uint32_t sy_n;
static volatile uint32_t sy_done;
static struct spinlock sy_lock;

static void sync_worker(void *arg) {
    uint32_t was = 0;
    for (uint32_t i = 0; i < 100000u; i++) {
        spin_lock_irqsave(&sy_lock, (uintptr_t)arg, &was);
        sy_n++;
        spin_unlock_irqrestore(&sy_lock, was);
    }
    arch_atomic_add_u32(&sy_done, 1u);
    thread_exit(0);
}

int sync_contention_selftest(void) {
    extern volatile uint32_t g_ticks;
    struct thread *w1;
    struct thread *w2;
    uint32_t t0;
    sy_n = 0;
    sy_done = 0;
    spin_init(&sy_lock, "sy-test");
    /* Scheduler + timer live (runs after the sleep selftest);
     * g_init_process survives it. Owner tokens are distinct. */
    w1 = thread_create(g_init_process, sync_worker, (void *)1, 16u);
    w2 = thread_create(g_init_process, sync_worker, (void *)2, 16u);
    if (w1 == 0 || w2 == 0) {
        serial_puts("SYNC-FAIL spawn\n");
        return -1;
    }
    t0 = g_ticks;
    while (sy_done != 2u) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("SYNC-FAIL stuck\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    /* Exactly 200k: no lost update survived the lock (a single
     * torn increment would show, deterministically). */
    if (sy_n != 200000u) {
        serial_puts("SYNC-FAIL count\n");
        return -1;
    }
    serial_puts("SYNC-OK\n");
    return NOVA_ESUCCESS;
}
