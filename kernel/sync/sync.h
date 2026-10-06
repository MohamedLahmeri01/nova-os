/* NOVA OS spinlock (Phase 0.3-E, SMP foundations).
 * Test-and-set over arch atomics, pause-spin. NOT reentrant
 * (self-deadlocks by design; owner field exists so tests and
 * diagnostics can prove who holds it). Two disciplines:
 * plain (caller handles IRQ exclusion, e.g. already-IF=0 or
 * data never touched in IRQ context) and irqsave (saves IF).
 * Sleep is forbidden while held (spins, never blocks).
 * APs stay parked: single-executor today, correct for SMP
 * tomorrow (LOCK primitives are bus-correct now, not later).
 */
#ifndef NOVA_SYNC_H
#define NOVA_SYNC_H

#include <stdint.h>

struct spinlock {
    volatile uint32_t locked; /* 0 free, 1 held */
    const char *name;         /* static string, for diagnostics */
    volatile uintptr_t owner; /* holder token (thread ptr), 0 free */
};

void spin_init(struct spinlock *l, const char *name);
/* Acquire (spins). owner = caller token (thread pointer). */
void spin_lock(struct spinlock *l, uintptr_t owner);
/* Try once: 0 acquired, -EBUSY held. Never spins. */
int spin_trylock(struct spinlock *l, uintptr_t owner);
void spin_unlock(struct spinlock *l);
/* IRQ-safe pair: lock with IF saved+cleared, unlock restores.
 * was_on carries the saved IF across (caller-owned word). */
void spin_lock_irqsave(struct spinlock *l, uintptr_t owner,
                       uint32_t *was_on);
void spin_unlock_irqrestore(struct spinlock *l, uint32_t was_on);
/* Contention proof (scheduler live): two threads, one counter,
 * exact total. Prints SYNC-OK. Nonzero on failure (fatal). */
int sync_contention_selftest(void);

#endif /* NOVA_SYNC_H */
