/* NOVA OS scheduler: preemptive round-robin (Phase 4a).
 * Circular runqueue (policy ops); slice accounting in sched_tick;
 * switching in sched_do_switch (IRQ path) and sched_yield (thread
 * path, cli/sti guarded). Incoming stacks are canary-checked.
 * Fairness/priority/SMP balancing arrive with Phase 4b (same ops).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/heap.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "process/process.h"

volatile int sched_need_resched;

static struct thread *g_current;
static struct thread *g_rr_head;
static struct thread *g_rr_tail;
static int g_started;

static void check_canary(struct thread *t) {
    if (t->stack_base != 0 &&
        *(volatile uint32_t *)t->stack_base != STACK_CANARY) {
        nova_panic("stack-overflow");
    }
}

static void rr_add(struct thread *t) {
    t->next = t;
    if (g_rr_head == 0) {
        g_rr_head = t;
        g_rr_tail = t;
    } else {
        t->next = g_rr_head;
        g_rr_tail->next = t;
        g_rr_tail = t;
    }
}

static void rr_remove(struct thread *t) {
    struct thread *p;
    if (g_rr_head == 0) {
        return;
    }
    p = g_rr_head;
    do {
        if (p->next == t) {
            p->next = t->next;
            if (g_rr_head == t) {
                g_rr_head = (t->next == t) ? 0 : t->next;
            }
            if (g_rr_tail == t) {
                g_rr_tail = (t->next == t) ? 0 : p;
            }
            t->next = 0;
            return;
        }
        p = p->next;
    } while (p != g_rr_head);
}

static struct thread *rr_pick(struct thread *cur) {
    (void)cur;
    if (g_rr_head == 0) {
        return 0;
    }
    return g_rr_head;
}

static const sched_policy_t rr_policy = { rr_add, rr_remove, rr_pick };
static const sched_policy_t *g_policy = &rr_policy;

void sched_init(void) {
    g_current = 0;
    g_rr_head = 0;
    g_rr_tail = 0;
    g_started = 0;
    sched_need_resched = 0;
}

void sched_add(struct thread *t) {
    g_policy->add(t);
}

void sched_remove(struct thread *t) {
    g_policy->remove(t);
}

struct thread *sched_pick(struct thread *cur) {
    if (cur == 0) {
        return g_rr_head;
    }
    /* Round-robin rotation: advance head past cur so the next runnable
     * is chosen (cur is guaranteed in the list here). */
    if (cur->next != 0) {
        g_rr_head = cur->next;
    }
    return g_policy->pick(cur);
}

void sched_enter(struct thread *t) {
    g_current = t;
    t->state = THREAD_RUNNING;
}

/* Adopt the boot stack as thread 0 (no PMM stack, never exits). */
struct thread *sched_adopt_boot(void) {
    struct thread *t;
    t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == 0) {
        nova_panic("sched-no-mem");
    }
    t->sp = 0; /* set on first switch-out */
    t->id = 0;
    t->state = THREAD_RUNNING;
    t->stack_base = 0;
    t->slice_used = 0;
    t->next = 0;
    t->pnext = 0;
    if (g_init_process == 0) {
        g_init_process = process_create();
    }
    if (g_init_process == 0) {
        nova_panic("sched-no-proc");
    }
    t->proc = g_init_process;
    process_attach(g_init_process, t);
    thread_count_inc();
    return t;
}

struct thread *sched_current_thread(void) {
    return g_current;
}

void sched_tick(void) {
    struct thread *cur;
    if (!g_started) {
        return;
    }
    cur = g_current;
    if (cur == 0) {
        return;
    }
    if (++cur->slice_used >= SCHED_SLICE_TICKS) {
        cur->slice_used = 0;
        sched_need_resched = 1;
    }
}

void sched_do_switch(void) {
    struct thread *cur;
    struct thread *next;
    if (!sched_need_resched) {
        return;
    }
    sched_need_resched = 0;
    thread_reap_graveyard();
    cur = g_current;
    if (cur == 0) {
        return;
    }
    next = sched_pick(cur);
    if (next == 0) {
        nova_panic("sched-empty");
    }
    if (next == cur) {
        return;
    }
    check_canary(next);
    sched_enter(next);
    ctx_switch(&cur->sp, &next->sp);
}

void sched_yield(void) {
    struct thread *cur;
    struct thread *next;
    __asm__ volatile("cli" ::: "memory");
    cur = g_current;
    next = (cur == 0) ? g_rr_head : sched_pick(cur);
    if (next != 0 && next != cur) {
        check_canary(next);
        sched_enter(next);
        ctx_switch(&cur->sp, &next->sp);
    }
    __asm__ volatile("sti" ::: "memory");
}

/* --- selftest: prove preemptive concurrency, deterministically ---
 * A runs 300M iterations without yielding; B/C run 100k each. All do
 * a few yields first (exercises the yield path), then run pure. With
 * timer preemption, B/C complete strictly inside A's tick window
 * (recorded start/end ticks); without it, A never lets go and the
 * watchdog fires. Overlap is asserted, not eyeballed. */

static volatile uint32_t st_done[3];
static volatile uint32_t st_cnt[3];
static volatile uint32_t st_start[3];
static volatile uint32_t st_end[3];

extern volatile uint32_t g_ticks;

static void worker_long(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < 5u; i++) {
        sched_yield();
    }
    st_start[0] = g_ticks;
    for (uint32_t i = 0; i < 300000000u; i++) {
        st_cnt[0]++;
    }
    st_end[0] = g_ticks;
    st_done[0] = 1;
    thread_exit(0);
}

static void worker_timed(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < 5u; i++) {
        sched_yield();
    }
    st_start[id] = g_ticks;
    for (uint32_t i = 0; i < 100000u; i++) {
        st_cnt[id]++;
    }
    st_end[id] = g_ticks;
    st_done[id] = 1;
    thread_exit(0);
}

int sched_selftest(void) {
    struct thread *boot;
    uint32_t t0;
    sched_init();
    boot = sched_adopt_boot();
    sched_add(boot);
    sched_enter(boot);
    if (thread_create(g_init_process, worker_long, 0) == 0 ||
        thread_create(g_init_process, worker_timed, (void *)1) == 0 ||
        thread_create(g_init_process, worker_timed, (void *)2) == 0) {
        nova_panic("sched-spawn-fail");
    }
    serial_puts("SCHED threads=");
    serial_putdec32(thread_count());
    serial_putc('\n');
    g_started = 1;
    t0 = g_ticks;
    while (!(st_done[0] && st_done[1] && st_done[2])) {
        if (g_ticks - t0 > 8000u) {
            nova_panic("sched-stuck");
        }
        __asm__ volatile("hlt");
    }
    /* Overlap proof: B/C ran entirely inside A's window. */
    if (!(st_start[1] >= st_start[0] && st_end[1] <= st_end[0] &&
          st_start[2] >= st_start[0] && st_end[2] <= st_end[0])) {
        nova_panic("sched-no-preempt");
    }
    if (st_cnt[0] != 300000000u || st_cnt[1] != 100000u ||
        st_cnt[2] != 100000u) {
        nova_panic("sched-count-mismatch");
    }
    serial_puts("SCHED preempt=1\nSCHED-OK\n");
    return 0;
}
