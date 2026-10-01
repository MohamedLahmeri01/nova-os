/* NOVA OS scheduler interface (Phase 4a).
 * Policy behind ops (ADR-0004); default is preemptive round-robin with
 * a fixed slice. need_resched is set by sched_tick (IRQ context) and
 * consumed in irq_common before iret; sched_yield switches directly.
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
int sched_selftest(void);

#endif /* NOVA_SCHED_H */