/* NOVA OS scheduler: strict priority + RR within level (Phase 4c).
 * 32 priority queues (higher number preempts); rotation inside the top
 * non-empty level. Slice accounting in sched_tick; switching through
 * sched_switch_to (CR3 to next address space + canary + ctx_switch).
 * No aging yet: strict priority can starve (documented, Phase 4d).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "gdt.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "sched: no arch_cpu backend for this architecture"
#endif

#define SCHED_LEVELS 32u

volatile int sched_need_resched;

static struct thread *g_current;
static struct thread *g_prio_head[SCHED_LEVELS];
static struct thread *g_prio_tail[SCHED_LEVELS];
static int g_started;
static uint32_t g_kernel_cr3;
static uint32_t g_fallback_stack;

static void check_canary(struct thread *t) {
    if (t->stack_base != 0 &&
        *(volatile uint32_t *)t->stack_base != STACK_CANARY) {
        nova_panic("stack-overflow");
    }
}

static void prio_add(struct thread *t) {
    uint32_t p = (t->prio >= SCHED_LEVELS) ? SCHED_LEVELS - 1u : t->prio;
    t->next = 0;
    if (g_prio_head[p] == 0) {
        g_prio_head[p] = t;
        g_prio_tail[p] = t;
    } else {
        g_prio_tail[p]->next = t;
        g_prio_tail[p] = t;
    }
}

static void prio_remove(struct thread *t) {
    uint32_t p = (t->prio >= SCHED_LEVELS) ? SCHED_LEVELS - 1u : t->prio;
    struct thread **link = &g_prio_head[p];
    while (*link != 0 && *link != t) {
        link = &(*link)->next;
    }
    if (*link == 0) {
        return;
    }
    *link = t->next;
    if (g_prio_tail[p] == t) {
        g_prio_tail[p] = 0;
        for (struct thread *q = g_prio_head[p]; q != 0; q = q->next) {
            if (q->next == 0) {
                g_prio_tail[p] = q;
            }
        }
        if (g_prio_head[p] == 0) {
            g_prio_tail[p] = 0;
        }
    }
    t->next = 0;
}

static struct thread *prio_pick(struct thread *cur) {
    (void)cur;
    for (uint32_t p = SCHED_LEVELS; p > 0; p--) {
        struct thread *h = g_prio_head[p - 1u];
        if (h != 0) {
            /* Rotate within the level for round-robin fairness. */
            if (h->next != 0) {
                g_prio_head[p - 1u] = h->next;
                g_prio_tail[p - 1u]->next = h;
                h->next = 0;
                g_prio_tail[p - 1u] = h;
            }
            return h;
        }
    }
    return 0;
}

static const sched_policy_t prio_policy = { prio_add, prio_remove,
                                            prio_pick };
static const sched_policy_t *g_policy = &prio_policy;

void sched_init(void) {
    g_current = 0;
    for (uint32_t p = 0; p < SCHED_LEVELS; p++) {
        g_prio_head[p] = 0;
        g_prio_tail[p] = 0;
    }
    g_started = 0;
    sched_need_resched = 0;
    g_kernel_cr3 = arch_read_cr3();
    /* Permanent TSS stack: TSS.ESP0 must ALWAYS be valid (a freed
     * kstack under a stale TSS corrupts the next CPL3 trap). Set on
     * every switch; fallback covers stackless threads (boot/main). */
    g_fallback_stack = pmm_alloc_contig(2);
    if (g_fallback_stack == 0) {
        nova_panic("sched-no-tss-stack");
    }
}

void sched_add(struct thread *t) {
    g_policy->add(t);
}

void sched_remove(struct thread *t) {
    g_policy->remove(t);
}

struct thread *sched_pick(struct thread *cur) {
    (void)cur;
    return g_policy->pick(cur);
}

/* Central switch: canary, address space, context. Every switch site
 * funnels here (yield, IRQ preemption, exit, user return). */
void sched_switch_to(struct thread *cur, struct thread *next) {
    check_canary(next);
    if (cur != 0) {
        check_canary(cur);
    }
    sched_enter(next);
    if (next->is_user && next->proc != 0) {
        arch_write_cr3(next->proc->cr3);
    } else {
        arch_write_cr3(g_kernel_cr3);
    }
    if (next->stack_base != 0) {
        tss_set_esp0(next->stack_base + THREAD_STACK_SIZE);
    } else {
        tss_set_esp0(g_fallback_stack + THREAD_STACK_SIZE);
    }
    ctx_switch(cur ? &cur->sp : &next->sp, &next->sp);
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
    t->prio = 0; /* boot/idle thread: runs only when nothing else can */
    t->is_user = 0;
    t->next = 0;
    t->pnext = 0;
    t->user_stack = 0;
    t->user_shared = 0;
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
    sched_switch_to(cur, next);
}

void sched_yield(void) {
    struct thread *cur;
    struct thread *next;
    __asm__ volatile("cli" ::: "memory");
    cur = g_current;
    next = sched_pick(cur);
    if (next != 0 && next != cur && cur != 0) {
        sched_switch_to(cur, next);
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

static void sched_prio_test(void);

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
    if (thread_create(g_init_process, worker_long, 0, 16u) == 0 ||
        thread_create(g_init_process, worker_timed, (void *)1, 16u) == 0 ||
        thread_create(g_init_process, worker_timed, (void *)2, 16u) == 0) {
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
    serial_puts("SCHED preempt=1\n");
    sched_prio_test();
    serial_puts("SCHED-OK\n");
    return 0;
}

/* --- priority proof: strict levels, RR within ---
 * A(30) runs 100M, B/C(10) 20M each, none yield. Strict priority means
 * A completes before B/C even start; B/C then share fairly. */

static volatile uint32_t pr_done[3];
static volatile uint32_t pr_cnt[3];
static volatile uint32_t pr_start[3];
static volatile uint32_t pr_end[3];

static void worker_prio(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    uint32_t n = (id == 0) ? 100000000u : 20000000u;
    pr_start[id] = g_ticks;
    for (uint32_t i = 0; i < n; i++) {
        pr_cnt[id]++;
    }
    pr_end[id] = g_ticks;
    pr_done[id] = 1;
    thread_exit(0);
}

static void sched_prio_test(void) {
    uint32_t t0 = g_ticks;
    if (thread_create(g_init_process, worker_prio, (void *)0, 30u) == 0 ||
        thread_create(g_init_process, worker_prio, (void *)1, 10u) == 0 ||
        thread_create(g_init_process, worker_prio, (void *)2, 10u) == 0) {
        nova_panic("sched-prio-spawn-fail");
    }
    while (!(pr_done[0] && pr_done[1] && pr_done[2])) {
        if (g_ticks - t0 > 8000u) {
            nova_panic("sched-prio-stuck");
        }
        __asm__ volatile("hlt");
    }
    /* A strictly first: B/C start at/after A ends. */
    if (!(pr_start[1] >= pr_end[0] && pr_start[2] >= pr_end[0])) {
        nova_panic("sched-prio-violation");
    }
    if (pr_cnt[0] != 100000000u || pr_cnt[1] != 20000000u ||
        pr_cnt[2] != 20000000u) {
        nova_panic("sched-prio-count");
    }
    serial_puts("SCHED prio=1\n");
}
