/* NOVA OS scheduler: strict priority + RR within level (Phase 4c).
 * 32 priority queues (higher number preempts); rotation inside the top
 * non-empty level. Slice accounting in sched_tick; switching through
 * sched_switch_to (CR3 to next address space + canary + ctx_switch).
 * No aging: strict priority can starve (measured by
 * sched_starve_selftest; fairness work needs those numbers first).
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
#include "process/caps.h"
#include "process/rlimit.h"
#include "mm/addrspace.h"
#include "net/net.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "sched: no arch_cpu backend for this architecture"
#endif

#define SCHED_LEVELS 32u

extern volatile uint32_t g_ticks;

volatile int sched_need_resched;

/* Completed context switches (every site funnels through
 * sched_switch_to: yield, preemption, exit, wake). First
 * observability counter; Phase G grows the family. */
volatile uint32_t g_sched_switches;

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
    g_sched_switches++;
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
    /* NULL clears (Phase 10a: selftests borrow g_current and put
     * back whatever they found, including nothing). */
    if (t == 0) {
        g_current = 0;
        return;
    }
    g_current = t;
    t->state = THREAD_RUNNING;
}

/* --- sleep/wakeup (blocking primitives) ---
 * Blocked threads leave the runqueue and link on the sleep list
 * (their `next` field, sleeper-owned while BLOCKED). Expiry runs
 * in sched_tick (IRQ context, stores only). Wakeup is a no-op on
 * anything not BLOCKED (safe against wake-after-gone). */

static struct thread *g_sleep_head;

static void sleep_unlink(struct thread *t) {
    struct thread **link = &g_sleep_head;
    while (*link != 0 && *link != t) {
        link = &(*link)->next;
    }
    if (*link != 0) {
        *link = t->next;
    }
    t->next = 0;
}

static void sleep_wake(struct thread *t, int timedout) {
    struct thread *cur;
    sleep_unlink(t);
    t->state = THREAD_READY;
    t->sleep_timed = 0;
    t->sleep_timedout = timedout;
    sched_add(t);
    /* Strict priority: a higher-prio wakeup preempts at the next
     * IRQ return (same mechanism as slice expiry). */
    if (g_started) {
        cur = g_current;
        if (cur == 0 || t->prio > cur->prio) {
            sched_need_resched = 1;
        }
    }
}

void sched_wakeup(struct thread *t) {
    __asm__ volatile("cli" ::: "memory");
    if (t != 0 && t->state == THREAD_BLOCKED) {
        sleep_wake(t, 0);
    }
    __asm__ volatile("sti" ::: "memory");
}

int sched_can_sleep(void) {
    int ok;
    __asm__ volatile("cli" ::: "memory");
    ok = g_started && g_current != 0;
    __asm__ volatile("sti" ::: "memory");
    return ok;
}

static void sleep_enter(uint32_t deadline, int timed) {
    struct thread *cur;
    struct thread *next;
    __asm__ volatile("cli" ::: "memory");
    cur = g_current;
    if (cur == 0) {
        nova_panic("sleep-no-thread");
    }
    cur->state = THREAD_BLOCKED;
    cur->sleep_deadline = deadline;
    cur->sleep_timed = timed;
    cur->sleep_timedout = 0;
    sched_remove(cur);
    /* Sleep list push-front (`next` was cleared by remove). */
    cur->next = g_sleep_head;
    g_sleep_head = cur;
    next = sched_pick(cur);
    if (next == 0 || next == cur) {
        nova_panic("sched-empty");
    }
    sched_switch_to(cur, next);
    __asm__ volatile("sti" ::: "memory");
}

void sched_sleep(void) {
    sleep_enter(0, 0);
}

int sched_sleep_until(uint32_t deadline) {
    struct thread *cur;
    sleep_enter(deadline, 1);
    /* Resumed here (same thread): the waker set the verdict. */
    cur = g_current;
    if (cur == 0) {
        nova_panic("sleep-no-thread");
    }
    return cur->sleep_timedout;
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
    t->sleep_deadline = 0;
    t->sleep_timed = 0;
    t->sleep_timedout = 0;
    if (g_init_process == 0) {
        g_init_process = process_create();
    }
    if (g_init_process == 0) {
        nova_panic("sched-no-proc");
    }
    /* The first process is root with all caps (Phase 10a): every
     * spawn inherits from here unless sandboxed down. */
    g_init_process->caps = CAPS_ALL;
    g_init_process->uid = UID_ROOT;
    g_init_process->gid = UID_ROOT;
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
    struct thread *t;
    struct thread *nx;
    if (!g_started) {
        return;
    }
    cur = g_current;
    if (cur == 0) {
        return;
    }
    /* Wire RX in IRQ context (100Hz promptness, no ISR nesting
     * hazard: this tick runs IF=0 to completion, and the pump
     * self-locks against thread drains. Socket waiters woken
     * here preempt at this tick's return. Idle cost is two ring
     * loads (EAGAIN short-circuits before any MMIO). */
    net_rx_pump();
    /* Sleep expiry (IRQ context, stores only): wake timed
     * sleepers at/past deadline (signed compare: wrap-safe). */
    t = g_sleep_head;
    while (t != 0) {
        nx = t->next;
        if (t->sleep_timed &&
            (int32_t)(g_ticks - t->sleep_deadline) >= 0) {
            sleep_wake(t, 1);
        }
        t = nx;
    }
    /* CPU accounting (Phase 13b): whoever runs burns their
     * process's ticks (kernel threads included; only user
     * threads are ever killed for it). */
    if (cur->proc != 0) {
        cur->proc->ticks_used++;
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
    /* CPU quota (Phase 13b): an over-limit user thread exits here
     * instead of switching back. Same footing as do_exit (own
     * stack current, user frames freed the same way, noreturn
     * switch away); the addrspace + code frames leak like every
     * user exit today (no process reaping yet). Kernel threads
     * are exempt (trusted); default-unlimited processes never
     * trip (ticks_used only grows). */
    if (cur->is_user && cur->proc != 0 &&
        cur->proc->ticks_used >= cur->proc->rlimit_ticks) {
        if (cur->user_stack != 0) {
            pmm_free_contig(cur->user_stack, 2);
        }
        if (cur->user_shared != 0) {
            pmm_free_frame(cur->user_shared);
        }
        thread_exit(24); /* SIGXCPU by analogy (code unread) */
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

/* --- CPU quota proof: a spinning user thread dies on schedule ---
 * Spinner runs at prio 1 (above the prio-0 boot thread, so it
 * preempts immediately and the boot thread observes from stall);
 * limit 50 ticks. Death observed via thread_count (the handoff
 * decrements before reaping); ticks_used proves accounting ran;
 * the upper bound proves promptness (slice is 10 ticks). */
#define SPIN_CODE 0x80000000u
#define SPIN_STACK_TOP 0x80400000u

static const uint8_t spin_blob[2] = { 0xEB, 0xFE }; /* jmp $ */

int sched_ticklimit_selftest(void) {
    extern volatile uint32_t g_ticks;
    struct process *p;
    struct addrspace *as;
    uint32_t code, stack;
    uint32_t base_count;
    uint32_t t0;
    p = process_create();
    as = addrspace_create();
    if (p == 0 || as == 0) {
        serial_puts("TICK-FAIL alloc\n");
        return -1;
    }
    p->cr3 = as->pd;
    p->rlimit_ticks = 50; /* harness privilege: direct set */
    code = pmm_alloc_frame();
    stack = pmm_alloc_contig(2);
    if (code == 0 || stack == 0) {
        serial_puts("TICK-FAIL mem\n");
        return -1;
    }
    ((uint8_t *)code)[0] = spin_blob[0];
    ((uint8_t *)code)[1] = spin_blob[1];
    if (addrspace_map(as, SPIN_CODE, code, 1) != 0 ||
        addrspace_map(as, SPIN_STACK_TOP - 8192u, stack, 1) != 0 ||
        addrspace_map(as, SPIN_STACK_TOP - 4096u, stack + 4096u,
                      1) != 0) {
        serial_puts("TICK-FAIL map\n");
        return -1;
    }
    base_count = thread_count();
    t0 = g_ticks;
    if (thread_create_user(p, SPIN_CODE, SPIN_STACK_TOP - 8u,
                           stack, 0, 1u) == 0) {
        serial_puts("TICK-FAIL spawn\n");
        return -1;
    }
    for (;;) {
        if (thread_count() < base_count + 1u) {
            break;
        }
        if (g_ticks - t0 > 1000u) {
            serial_puts("TICK-FAIL stuck\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    /* Ran its quota, died promptly (slice 10 + wide margin). */
    if (p->ticks_used < 50u || p->ticks_used > 200u) {
        serial_puts("TICK-FAIL count\n");
        return -1;
    }
    pmm_free_frame(code);
    pmm_free_contig(stack, 2);
    addrspace_destroy(as);
    kfree(p);
    serial_puts("TICK-OK\n");
    return 0;
}

/* --- sleep/wakeup proof: early wake x2 + timeout, deterministic ---
 * Worker (prio 16, above boot 0) parks with a far deadline twice
 * (main wakes it early) then sleeps 30 ticks with no waker
 * (expiry must fire). Return codes are the verdicts (0 woken,
 * 1 expired); the timeout window (30..500) proves promptness
 * without racing the tick granularity. */

static volatile int sl_done;
static volatile int sl_phase;
static volatile uint32_t sl_entry;
static volatile uint32_t sl_wake;
static volatile int sl_rc0;
static volatile int sl_rc1;
static volatile int sl_rc2;
static struct thread *sl_worker;

static void sleep_worker(void *arg) {
    (void)arg;
    sl_entry = g_ticks;
    sl_rc0 = sched_sleep_until(sl_entry + 500u); /* woken early */
    sl_wake = g_ticks;
    sl_phase = 1;
    sl_rc1 = sched_sleep_until(g_ticks + 500u); /* woken early */
    sl_phase = 2;
    sl_entry = g_ticks;
    sl_rc2 = sched_sleep_until(sl_entry + 30u); /* expires */
    sl_wake = g_ticks;
    sl_phase = 3;
    sl_done = 1;
    thread_exit(0);
}

static int sleep_wait_blocked(uint32_t t0) {
    while (sl_worker->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 2000u) {
            return -1;
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

int sched_sleep_selftest(void) {
    uint32_t t0;
    sl_done = 0;
    sl_phase = 0;
    sl_entry = 0;
    sl_wake = 0;
    sl_rc0 = -1;
    sl_rc1 = -1;
    sl_rc2 = -1;
    /* Scheduler + timer already live (runs after sched_selftest);
     * g_init_process survives it. */
    sl_worker = thread_create(g_init_process, sleep_worker, 0, 16u);
    if (sl_worker == 0) {
        serial_puts("SLEEP-FAIL spawn\n");
        return -1;
    }
    sched_yield(); /* worker runs to its first sleep */
    /* Phase 0: blocked with a far deadline, woken early. */
    t0 = g_ticks;
    if (sleep_wait_blocked(t0) != 0 || sl_phase != 0) {
        serial_puts("SLEEP-FAIL park0\n");
        return -1;
    }
    sched_wakeup(sl_worker);
    /* Phase 1: blocked again, woken early again. */
    t0 = g_ticks;
    while (sl_phase != 1) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SLEEP-FAIL phase1\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    t0 = g_ticks;
    if (sleep_wait_blocked(t0) != 0) {
        serial_puts("SLEEP-FAIL park1\n");
        return -1;
    }
    sched_wakeup(sl_worker);
    /* Phase 2: no waker; the 30-tick deadline must expire. */
    t0 = g_ticks;
    while (!sl_done) {
        if (g_ticks - t0 > 2000u) {
            serial_puts("SLEEP-FAIL expire\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    /* Early wakes return 0 (not expired); the timeout returns 1,
     * inside its window (not early, not lost). */
    if (sl_rc0 != 0 || sl_rc1 != 0) {
        serial_puts("SLEEP-FAIL early\n");
        return -1;
    }
    if (sl_rc2 != 1) {
        serial_puts("SLEEP-FAIL timeout\n");
        return -1;
    }
    if (sl_wake - sl_entry < 30u || sl_wake - sl_entry > 500u) {
        serial_puts("SLEEP-FAIL window\n");
        return -1;
    }
    serial_puts("SLEEP-OK\n");
    return 0;
}

/* --- starvation measurement: strict priority contains low work ---
 * Hog (prio 31) burns 30M iterations without yielding; worker
 * (prio 1) burns 100k then exits. The worker's first act records
 * whether the hog had finished: under strict priority it always
 * has (early == 0, starvation demonstrated deterministically by
 * ORDER, not timing). Any future fair policy must flip early to
 * 1 -- this test is the tripwire. Also reports hog tick cost +
 * switch count (first latency/throughput numbers in-tree). */

static volatile uint32_t sv_hog_n;
static volatile uint32_t sv_work_n;
static volatile int sv_phase;
static volatile int sv_early;
static volatile int sv_work_done;
static volatile uint32_t sv_t0;
static volatile uint32_t sv_t1;

static void starve_hog(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < 300000000u; i++) {
        sv_hog_n++;
    }
    sv_phase = 1;
    thread_exit(0);
}

static void starve_work(void *arg) {
    (void)arg;
    if (sv_phase != 1) {
        sv_early = 1;
    }
    for (uint32_t i = 0; i < 100000u; i++) {
        sv_work_n++;
    }
    sv_work_done = 1;
    thread_exit(0);
}

int sched_starve_selftest(void) {
    extern volatile uint32_t g_ticks;
    uint32_t sw0;
    uint32_t t0;
    sv_hog_n = 0;
    sv_work_n = 0;
    sv_phase = 0;
    sv_early = 0;
    sv_work_done = 0;
    if (thread_create(g_init_process, starve_hog, 0, 31u) == 0 ||
        thread_create(g_init_process, starve_work, 0, 1u) == 0) {
        serial_puts("STARVE-FAIL spawn\n");
        return -1;
    }
    sw0 = g_sched_switches;
    sv_t0 = g_ticks;
    sched_yield(); /* hog (top prio) preempts immediately */
    t0 = g_ticks;
    while (!sv_work_done) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("STARVE-FAIL stuck\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    sv_t1 = g_ticks;
    /* Worker ran strictly after the hog (starved throughout),
     * did its full 100k, and the hog did its full 30M. */
    if (sv_early != 0) {
        serial_puts("STARVE-FAIL early\n");
        return -1;
    }
    if (sv_hog_n != 300000000u || sv_work_n != 100000u) {
        serial_puts("STARVE-FAIL count\n");
        return -1;
    }
    serial_puts("STARVE hog_ticks=");
    serial_putdec32(sv_t1 - sv_t0);
    serial_puts(" switches=");
    serial_putdec32(g_sched_switches - sw0);
    serial_puts(" early=0 done=1\n");
    serial_puts("STARVE-OK\n");
    return 0;
}
