/* NOVA OS scheduler interface (Phase 4a, strict-prio since 4c).
 * Policy behind ops (ADR-0004, ADR-0016); current default is
 * strict 32-level priority + RR within level, 10-tick slices.
 * need_resched is set by sched_tick (IRQ context) and consumed
 * in irq_common before iret; sched_yield switches directly.
 * No aging: strict priority can starve (measured, not wished;
 * see sched_starve_selftest).
 */
#ifndef NOVA_SCHED_H
#define NOVA_SCHED_H

#include <stdint.h>

struct thread;

typedef struct {
    void (*add)(struct thread *t);
    void (*remove)(struct thread *t);
    struct thread *(*pick)(struct thread *cur);
} sched_policy_t;

#define SCHED_SLICE_TICKS 10u

extern volatile int sched_need_resched;
/* Completed context switches (see sched.c). */
extern volatile uint32_t g_sched_switches;

void sched_init(void);
struct thread *sched_adopt_boot(void);
void sched_add(struct thread *t);
void sched_remove(struct thread *t);
struct thread *sched_pick(struct thread *cur);
void sched_enter(struct thread *t);
struct thread *sched_current_thread(void);
void sched_tick(void);
void sched_do_switch(void);
void sched_yield(void);
void sched_switch_to(struct thread *cur, struct thread *next);
/* Sleep until woken (no timeout). The sleeper leaves the
 * runqueue; at least one other runnable thread must exist
 * (sleeping last panics like sched-empty). IRQ-safe to enter
 * from thread context only (not from an IRQ handler). */
void sched_sleep(void);
/* Sleep until woken or absolute tick deadline (wrap-safe).
 * Returns 0 woken, 1 expired. Same runnable-other precondition. */
int sched_sleep_until(uint32_t deadline);
/* Wake a blocked thread (no-op otherwise: safe against
 * wake-after-gone races). IRQ-safe (stores only); a woken
 * higher-prio thread preempts at the next IRQ return. */
void sched_wakeup(struct thread *t);
/* Nonzero when sleeping is legal (scheduler started with a
 * current thread). Advisory fast check for paths (early boot)
 * that must spin instead. */
int sched_can_sleep(void);
int sched_selftest(void);
/* CPU quota proof: a spinning user thread dies on schedule.
 * Prints TICK-OK. Nonzero on failure (fatal: panic). */
int sched_ticklimit_selftest(void);
/* Sleep/wakeup proof: early wake x2 + timeout expiry.
 * Prints SLEEP-OK. Nonzero on failure (fatal: panic). */
int sched_sleep_selftest(void);
/* Starvation measurement: a prio-31 hog must strictly contain a
 * prio-1 worker (the worker records whether it ever ran early).
 * Prints STARVE-OK with hog ticks + switch count. Nonzero on
 * failure (fatal: panic). */
int sched_starve_selftest(void);
/* Deterministic workload battery (RR pair, many-runnable, wake
 * latency, sleep accuracy). Machine-readable SCHED-W lines.
 * Prints SCHEDW-OK. Nonzero on failure (fatal: panic). */
int sched_workload_selftest(void);

#endif /* NOVA_SCHED_H */