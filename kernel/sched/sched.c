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
#include "trace/trace.h"
#include "syscall/syscall.h"
#include "nova/types.h"

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
    trace_emit(TRACE_WAKE, (uint16_t)(t->id & 0xFFFFu),
               (uint32_t)timedout);
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
    trace_emit(TRACE_SLEEP, (uint16_t)(cur->id & 0xFFFFu), deadline);
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
    t->runtime_ticks = 0;
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

int sched_thread_stat(uint32_t id, uint32_t *runtime, int *state,
                      uint32_t *prio) {
    struct thread *t;
    uint32_t p;
    int found = 0;
    uint32_t rt = 0;
    int st = 0;
    uint32_t pr = 0;
    if (runtime == 0 || state == 0 || prio == 0) {
        return 0;
    }
    __asm__ volatile("cli" ::: "memory");
    if (g_current != 0 && g_current->id == id) {
        rt = g_current->runtime_ticks;
        st = g_current->state;
        pr = g_current->prio;
        found = 1;
    }
    for (p = 0; p < SCHED_LEVELS && !found; p++) {
        for (t = g_prio_head[p]; t != 0; t = t->next) {
            if (t->id == id) {
                rt = t->runtime_ticks;
                st = t->state;
                pr = t->prio;
                found = 1;
                break;
            }
        }
    }
    for (t = g_sleep_head; t != 0 && !found; t = t->next) {
        if (t->id == id) {
            rt = t->runtime_ticks;
            st = t->state;
            pr = t->prio;
            found = 1;
            break;
        }
    }
    __asm__ volatile("sti" ::: "memory");
    if (!found) {
        return 0;
    }
    *runtime = rt;
    *state = st;
    *prio = pr;
    return 1;
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
     * threads are ever killed for it). Phase F: the thread
     * itself accrues the same delivered tick (per-thread
     * runtime; IRQ work charges the preempted thread —
     * unattributable at tick granularity, documented). */
    if (cur->proc != 0) {
        cur->proc->ticks_used++;
    }
    cur->runtime_ticks++;
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
    uint32_t base_count;
    sv_hog_n = 0;
    sv_work_n = 0;
    sv_phase = 0;
    sv_early = 0;
    sv_work_done = 0;
    base_count = thread_count();
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
    /* Lifecycle accounting (Phase G re-audit): both workers exited;
     * drain the graveyard and prove the count returns to base
     * (no leaked threads, no double-decrement). */
    thread_reap_graveyard();
    if (thread_count() != base_count) {
        serial_puts("STARVE-FAIL threads\n");
        return -1;
    }
    serial_puts("STARVE-OK\n");
    return 0;
}

/* --- deterministic workload battery (v0.4 Phase E): RR fairness,
 * scale, wake latency, sleep accuracy. Machine-readable SCHED-W
 * lines on serial (the grep-able results channel; guests cannot
 * hand files to the host). All bounds deterministic; watchdogs
 * fail loud. Prints SCHEDW-OK. */

static volatile uint32_t wl_s[2];
static volatile uint32_t wl_e[2];
static volatile uint32_t wl_c[2];
static volatile int wl_done[2];

static void wl_rr(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    wl_s[id] = g_ticks;
    for (uint32_t i = 0; i < 200000u; i++) {
        wl_c[id]++;
    }
    wl_e[id] = g_ticks;
    wl_done[id] = 1;
    thread_exit(0);
}

static volatile uint32_t wm_n[6];
static volatile uint32_t wm_done;

static void wl_many(void *arg) {
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < 50000u; i++) {
        wm_n[id]++;
    }
    arch_atomic_add_u32(&wm_done, 1u);
    thread_exit(0);
}

static volatile uint32_t ww_t_wake;
static volatile uint32_t ww_t_resume;
static volatile int ww_done;
static struct thread *ww_w;

static void wl_waiter(void *arg) {
    uint32_t d;
    (void)arg;
    d = g_ticks + 50u;
    sched_sleep_until(d);
    ww_t_resume = g_ticks;
    ww_done = 1;
    thread_exit(0);
}

static volatile uint32_t ws_t0;
static volatile uint32_t ws_t1;
static volatile int ws_done;

static void wl_sleeper(void *arg) {
    uint32_t d;
    (void)arg;
    ws_t0 = g_ticks;
    d = ws_t0 + 20u;
    sched_sleep_until(d);
    ws_t1 = g_ticks;
    ws_done = 1;
    thread_exit(0);
}

static int wl_wait_done(volatile int *flag, uint32_t t0,
                        const char *what) {
    while (!*flag) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("SCHEDW-FAIL ");
            serial_puts(what);
            serial_puts("\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

int sched_workload_selftest(void) {
    extern volatile uint32_t g_ticks;
    uint32_t t0;
    uint32_t sw0;
    /* W1 RR-equal: same-level pair must interleave (slice
     * rotation), both complete exact counts. */
    wl_s[0] = 0;
    wl_s[1] = 0;
    wl_e[0] = 0;
    wl_e[1] = 0;
    wl_c[0] = 0;
    wl_c[1] = 0;
    wl_done[0] = 0;
    wl_done[1] = 0;
    if (thread_create(g_init_process, wl_rr, (void *)0, 16u) == 0 ||
        thread_create(g_init_process, wl_rr, (void *)1, 16u) == 0) {
        serial_puts("SCHEDW-FAIL spawn-rr\n");
        return -1;
    }
    sw0 = g_sched_switches;
    t0 = g_ticks;
    if (wl_wait_done(&wl_done[0], t0, "rr0") != 0 ||
        wl_wait_done(&wl_done[1], t0, "rr1") != 0) {
        return -1;
    }
    if (wl_c[0] != 200000u || wl_c[1] != 200000u) {
        serial_puts("SCHEDW-FAIL rr-count\n");
        return -1;
    }
    /* Overlap either direction (whoever ran first, the other
     * started before the first finished): rotation proven. */
    if (!((wl_s[1] <= wl_e[0] && wl_s[0] <= wl_e[1]))) {
        serial_puts("SCHEDW-FAIL rr-fair\n");
        return -1;
    }
    serial_puts("SCHED-W rr overlap=1 switches=");
    serial_putdec32(g_sched_switches - sw0);
    serial_putc('\n');
    /* W2 many-runnable: six same-level workers all finish
     * exact counts (rotation scales past pairs). */
    wm_done = 0;
    for (uint32_t i = 0; i < 6u; i++) {
        wm_n[i] = 0;
        if (thread_create(g_init_process, wl_many, (void *)i,
                          8u) == 0) {
            serial_puts("SCHEDW-FAIL spawn-many\n");
            return -1;
        }
    }
    sw0 = g_sched_switches;
    t0 = g_ticks;
    while (wm_done != 6u) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("SCHEDW-FAIL many\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    for (uint32_t i = 0; i < 6u; i++) {
        if (wm_n[i] != 50000u) {
            serial_puts("SCHEDW-FAIL many-count\n");
            return -1;
        }
    }
    serial_puts("SCHED-W many n=6 switches=");
    serial_putdec32(g_sched_switches - sw0);
    serial_putc('\n');
    /* W3 wake latency: main wakes a +50 sleeper early; resume
     * lands within 2 ticks (10ms granularity: 0-1 typical,
     * bound never exceeded without a scheduling stall). */
    ww_done = 0;
    ww_t_wake = 0;
    ww_t_resume = 0;
    ww_w = thread_create(g_init_process, wl_waiter, 0, 16u);
    if (ww_w == 0) {
        serial_puts("SCHEDW-FAIL spawn-wait\n");
        return -1;
    }
    sched_yield();
    t0 = g_ticks;
    while (ww_w->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("SCHEDW-FAIL wait-park\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    ww_t_wake = g_ticks;
    sched_wakeup(ww_w);
    t0 = g_ticks;
    if (wl_wait_done(&ww_done, t0, "wait-done") != 0) {
        return -1;
    }
    if (ww_t_resume - ww_t_wake > 2u) {
        serial_puts("SCHEDW-FAIL wait-lat\n");
        return -1;
    }
    serial_puts("SCHED-W wake latency=");
    serial_putdec32(ww_t_resume - ww_t_wake);
    serial_puts(" bound=2\n");
    /* W4 sleep accuracy: +20 deadline fires in-window
     * (tick-exact expiry, generous upper bound). */
    ws_done = 0;
    if (thread_create(g_init_process, wl_sleeper, 0, 16u) == 0) {
        serial_puts("SCHEDW-FAIL spawn-sleep\n");
        return -1;
    }
    t0 = g_ticks;
    if (wl_wait_done(&ws_done, t0, "sleep-done") != 0) {
        return -1;
    }
    if (ws_t1 - ws_t0 < 20u || ws_t1 - ws_t0 > 25u) {
        serial_puts("SCHEDW-FAIL sleep-win\n");
        return -1;
    }
    serial_puts("SCHED-W sleep asked=20 actual=");
    serial_putdec32(ws_t1 - ws_t0);
    serial_putc('\n');
    serial_puts("SCHEDW-OK\n");
    return 0;
}

/* --- Phase F runtime accounting selftest ---
 * Per-thread delivered-tick accounting + SYS_RUNTIME ABI.
 * Fresh process P isolates arithmetic: P.ticks_used must
 * EXACTLY equal the sum of its threads' final runtimes
 * (same increment site; exited threads persist in P).
 * Workers gate on their own runtime (deterministic under
 * any tick rate); ranges allow +2 for park-path ticks. */
static volatile uint32_t rt_d_done, rt_d_saved, rt_d_id;
static volatile uint32_t rt_l_early, rt_h_done;
static struct thread *rt_ta, *rt_tb, *rt_tc, *rt_th, *rt_tl;
static struct process *rt_p;

static uint32_t rt_runtime(struct thread *t) {
    return *(volatile uint32_t *)&t->runtime_ticks;
}

static void rt_spinner(void *arg) {
    struct thread *me = thread_current();
    (void)arg;
    while (rt_runtime(me) < 60u) {
    }
    sched_sleep();
    thread_exit(0);
}

static void rt_sleeper(void *arg) {
    (void)arg;
    sched_sleep_until(g_ticks + 500u);
    thread_exit(0);
}

static void rt_exiter(void *arg) {
    struct thread *me = thread_current();
    (void)arg;
    while (rt_runtime(me) < 20u) {
    }
    rt_d_saved = rt_runtime(me);
    rt_d_id = me->id;
    rt_d_done = 1;
    thread_exit(0);
}

static void rt_hog(void *arg) {
    struct thread *me = thread_current();
    (void)arg;
    while (rt_runtime(me) < 30u) {
    }
    rt_h_done = 1;
    sched_sleep();
    thread_exit(0);
}

static void rt_low(void *arg) {
    struct thread *me = thread_current();
    (void)arg;
    if (!rt_h_done) {
        rt_l_early = 1;
    }
    while (rt_runtime(me) < 10u) {
    }
    sched_sleep();
    thread_exit(0);
}

static int rt_wait_parked(struct thread *t, const char *tag) {
    uint32_t t0 = g_ticks;
    while (t->state != THREAD_BLOCKED) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("RUNTIME-FAIL park-");
            serial_puts(tag);
            serial_putc('\n');
            return -1;
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

static int rt_wait_flag(volatile uint32_t *flag, const char *tag) {
    uint32_t t0 = g_ticks;
    while (!*flag) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("RUNTIME-FAIL flag-");
            serial_puts(tag);
            serial_putc('\n');
            return -1;
        }
        __asm__ volatile("hlt");
    }
    return 0;
}

int sched_runtime_selftest(void) {
    uint32_t base, t0, ra, rb, rc0, rh, rl;
    struct thread *me = thread_current();
    struct syscall_frame f;
    struct addrspace *as;
    uint32_t f1, f2, saved;
    uint8_t *k1;
    struct nova_runtime *ku;
    base = thread_count();
    rt_d_done = 0;
    rt_l_early = 0;
    rt_h_done = 0;
    rt_p = process_create();
    if (rt_p == 0) {
        serial_puts("RUNTIME-FAIL alloc-proc\n");
        return -1;
    }
    /* D: exit persistence (share stays in P). */
    if (thread_create(rt_p, rt_exiter, 0, 16u) == 0) {
        serial_puts("RUNTIME-FAIL spawn-d\n");
        return -1;
    }
    if (rt_wait_flag(&rt_d_done, "d") != 0) {
        return -1;
    }
    if (rt_d_saved < 20u || rt_d_saved > 22u) {
        serial_puts("RUNTIME-FAIL d-range\n");
        return -1;
    }
    /* A/B spinners + C long sleeper (fresh process P). */
    rt_ta = thread_create(rt_p, rt_spinner, 0, 16u);
    rt_tb = thread_create(rt_p, rt_spinner, 0, 16u);
    rt_tc = thread_create(rt_p, rt_sleeper, 0, 16u);
    if (rt_ta == 0 || rt_tb == 0 || rt_tc == 0) {
        serial_puts("RUNTIME-FAIL spawn-abc\n");
        return -1;
    }
    if (rt_wait_parked(rt_ta, "a") != 0 ||
        rt_wait_parked(rt_tb, "b") != 0 ||
        rt_wait_parked(rt_tc, "c") != 0) {
        return -1;
    }
    ra = rt_runtime(rt_ta);
    rb = rt_runtime(rt_tb);
    rc0 = rt_runtime(rt_tc);
    if (ra < 60u || ra > 62u || rb < 60u || rb > 62u) {
        serial_puts("RUNTIME-FAIL spin-range\n");
        return -1;
    }
    if (rc0 > 5u) {
        serial_puts("RUNTIME-FAIL sleep-charge\n");
        return -1;
    }
    /* Parked threads frozen while 30 ticks elapse. */
    t0 = g_ticks;
    while (g_ticks - t0 < 30u) {
        __asm__ volatile("hlt");
    }
    if (rt_runtime(rt_ta) != ra || rt_runtime(rt_tb) != rb ||
        rt_runtime(rt_tc) != rc0) {
        serial_puts("RUNTIME-FAIL frozen\n");
        return -1;
    }
    /* Exact aggregation (all of P alive or persisted). */
    if (rt_p->ticks_used != ra + rb + rc0 + rt_d_saved) {
        serial_puts("RUNTIME-FAIL aggr\n");
        return -1;
    }
    serial_puts("RUNTIME proc=");
    serial_putdec32(rt_p->ticks_used);
    serial_puts(" sum=");
    serial_putdec32(ra + rb + rc0 + rt_d_saved);
    serial_putc('\n');
    /* H/L strict priority: L never runs before H parks. */
    rt_th = thread_create(rt_p, rt_hog, 0, 31u);
    rt_tl = thread_create(rt_p, rt_low, 0, 1u);
    if (rt_th == 0 || rt_tl == 0) {
        serial_puts("RUNTIME-FAIL spawn-hl\n");
        return -1;
    }
    if (rt_wait_parked(rt_th, "h") != 0 ||
        rt_wait_parked(rt_tl, "l") != 0) {
        return -1;
    }
    rh = rt_runtime(rt_th);
    rl = rt_runtime(rt_tl);
    if (rh < 30u || rh > 32u || rl < 10u || rl > 12u ||
        rt_l_early) {
        serial_puts("RUNTIME-FAIL prio\n");
        return -1;
    }
    serial_puts("RUNTIME hog=");
    serial_putdec32(rh);
    serial_puts(" low=");
    serial_putdec32(rl);
    serial_puts(" early=");
    serial_putdec32(rt_l_early);
    serial_putc('\n');
    /* ABI block: one RW page + one RO view of another frame. */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("RUNTIME-FAIL abi-alloc\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0) {
        serial_puts("RUNTIME-FAIL abi-map\n");
        goto abi_fail;
    }
    k1 = (uint8_t *)f1;
    ku = (struct nova_runtime *)USER_BASE;
    for (uint32_t i = 0; i < 48u; i++) {
        k1[i] = 0x5Au;
    }
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.edx = 0;
    /* Self (tid 0): caller identity + monotonic runtime. */
    {
        uint32_t before = rt_runtime(me);
        f.ebx = 0;
        f.ecx = USER_BASE;
        f.eax = SYS_RUNTIME;
        syscall_handler(&f);
        if ((int32_t)f.eax != 0 || ku->tid != me->id ||
            ku->runtime < before || ku->state != THREAD_RUNNING ||
            ku->prio != me->prio) {
            serial_puts("RUNTIME-FAIL abi-self\n");
            goto abi_fail_cr3;
        }
    }
    /* Parked thread A: frozen values, BLOCKED state. */
    f.ebx = rt_ta->id;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0 || ku->tid != rt_ta->id ||
        ku->runtime != ra || ku->state != THREAD_BLOCKED ||
        ku->prio != 16u) {
        serial_puts("RUNTIME-FAIL abi-parked\n");
        goto abi_fail_cr3;
    }
    /* Exact 16B write (last fetch was A: tid echoes at
     * bytes 0..3; canary past byte 15 holds). */
    if (*(uint32_t *)k1 != rt_ta->id) {
        serial_puts("RUNTIME-FAIL abi-bytes\n");
        goto abi_fail_cr3;
    }
    for (uint32_t i = 16u; i < 48u; i++) {
        if (k1[i] != 0x5Au) {
            serial_puts("RUNTIME-FAIL abi-canary\n");
            goto abi_fail_cr3;
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    /* Refusals on kernel tables (no touch by construction).
     * do_runtime(tid=ebx, out=ecx): EFAULT cases use a valid
     * self tid with a bad out pointer. */
    f.ebx = 0;
    f.ecx = 0;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("RUNTIME-FAIL abi-null\n");
        goto abi_fail;
    }
    f.ecx = 0x1000u;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("RUNTIME-FAIL abi-kern\n");
        goto abi_fail;
    }
    f.ecx = 0xFFFFFFF0u;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("RUNTIME-FAIL abi-wrap\n");
        goto abi_fail;
    }
    f.ecx = USER_BASE + 4096u - 8u;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("RUNTIME-FAIL abi-cross\n");
        goto abi_fail;
    }
    f.ebx = 0x00DEAD00u;
    f.ecx = USER_BASE;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_ENOENT) {
        serial_puts("RUNTIME-FAIL abi-unknown\n");
        goto abi_fail;
    }
    f.ebx = rt_d_id;
    f.eax = SYS_RUNTIME;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_ENOENT) {
        serial_puts("RUNTIME-FAIL abi-exited\n");
        goto abi_fail;
    }
    /* RO view refuses, frame untouched. */
    pmm_free_frame(f2);
    f2 = 0;
    {
        struct addrspace *as2 = addrspace_create();
        uint32_t fr = pmm_alloc_frame();
        if (as2 == 0 || fr == 0) {
            serial_puts("RUNTIME-FAIL abi-ro-alloc\n");
            goto abi_fail;
        }
        if (addrspace_map(as2, USER_BASE, fr, 0) != 0) {
            serial_puts("RUNTIME-FAIL abi-ro-map\n");
            pmm_free_frame(fr);
            addrspace_destroy(as2);
            goto abi_fail;
        }
        *(uint8_t *)fr = 0x77u;
        __asm__ volatile("cli" ::: "memory");
        saved = arch_read_cr3();
        arch_write_cr3(as2->pd);
        f.ebx = 0;
        f.ecx = USER_BASE;
        f.eax = SYS_RUNTIME;
        syscall_handler(&f);
        arch_write_cr3(saved);
        __asm__ volatile("sti" ::: "memory");
        if ((int32_t)f.eax != -NOVA_EFAULT ||
            *(uint8_t *)fr != 0x77u) {
            serial_puts("RUNTIME-FAIL abi-ro\n");
            pmm_free_frame(fr);
            addrspace_destroy(as2);
            goto abi_fail;
        }
        pmm_free_frame(fr);
        addrspace_destroy(as2);
    }
    pmm_free_frame(f1);
    addrspace_destroy(as);
    /* Release all parked workers; they exit. */
    sched_wakeup(rt_ta);
    sched_wakeup(rt_tb);
    sched_wakeup(rt_tc);
    sched_wakeup(rt_th);
    sched_wakeup(rt_tl);
    t0 = g_ticks;
    while (thread_count() != base) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("RUNTIME-FAIL reap\n");
            return -1;
        }
        __asm__ volatile("hlt");
    }
    serial_puts("RUNTIME-OK\n");
    return 0;
abi_fail_cr3:
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
abi_fail:
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    return -1;
}
