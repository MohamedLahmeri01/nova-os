/* NOVA OS threads (Phase 4a).
 * Creation builds the initial kernel context (regs zeroed, eflags IF,
 * return to thread_trampoline with fn/arg above it). The boot thread
 * is adopted (no PMM stack, never exits). Exit reaps the previous
 * graveyard occupant first (a dying stack is never freed under
 * itself), unlinks, stashes self, and switches away forever.
 * thread_exit/yield cli around pick+switch so a timer IRQ cannot
 * interleave a stale pick (resume path restores/sti's explicitly).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"

extern void thread_trampoline(void);

static uint32_t g_next_id = 1;
static uint32_t g_thread_count;
static struct thread *g_graveyard;

void thread_reap_graveyard(void) {
    if (g_graveyard != 0) {
        struct thread *t = g_graveyard;
        g_graveyard = 0;
        if (t->stack_base != 0) {
            pmm_free_contig(t->stack_base, THREAD_STACK_FRAMES);
        }
        kfree(t);
    }
}

/* Shared zombie handoff (kernel exit + user-0x81 paths): reap the
 * previous occupant, unlink, account, stash. Caller switches away. */
void thread_zombie_handoff(struct thread *t) {
    thread_reap_graveyard();
    t->state = THREAD_ZOMBIE;
    sched_remove(t);
    g_thread_count--;
    g_graveyard = t;
}

struct thread *thread_create(struct process *p, thread_fn_t fn, void *arg,
                             uint8_t prio) {
    struct thread *t;
    uint32_t stack;
    if (g_thread_count >= THREAD_MAX || p == 0 || fn == 0) {
        return 0;
    }
    t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == 0) {
        return 0;
    }
    stack = pmm_alloc_contig(THREAD_STACK_FRAMES);
    if (stack == 0) {
        kfree(t);
        return 0;
    }
    *(uint32_t *)stack = STACK_CANARY;
    {
        /* Low-to-high must mirror ctx_switch saves: regs, eflags,
         * segs, ret (resume pops segs, eflags, regs, ret). */
        uint32_t *sp = (uint32_t *)(stack + THREAD_STACK_SIZE);
        *--sp = (uint32_t)arg;
        *--sp = (uint32_t)fn;
        *--sp = (uint32_t)&thread_trampoline;
        for (int k = 0; k < 8; k++) {
            *--sp = 0;
        }
        *--sp = 0x202u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        t->sp = sp;
    }
    t->id = g_next_id++;
    t->state = THREAD_READY;
    t->stack_base = stack;
    t->slice_used = 0;
    t->prio = (prio >= 32u) ? 31u : prio;
    t->is_user = 0;
    t->next = 0;
    t->pnext = 0;
    t->proc = p;
    t->user_stack = 0;
    t->user_shared = 0;
    process_attach(p, t);
    sched_add(t);
    g_thread_count++;
    return t;
}

/* Scheduled user thread: kstack trampoline frame leads into
 * user_entry_trampoline (iret to ring 3); see ctx.S. */
extern void user_entry_trampoline(void);

struct thread *thread_create_user(struct process *p, uint32_t eip,
                                  uint32_t uesp, uint32_t ustack_base,
                                  uint32_t shared_phys, uint8_t prio) {
    struct thread *t;
    uint32_t stack;
    uint32_t *tss_slot;
    extern uint32_t *tss_esp0_slot(void);
    if (g_thread_count >= THREAD_MAX || p == 0 || p->cr3 == 0) {
        return 0;
    }
    t = (struct thread *)kmalloc(sizeof(struct thread));
    if (t == 0) {
        return 0;
    }
    stack = pmm_alloc_contig(THREAD_STACK_FRAMES);
    if (stack == 0) {
        kfree(t);
        return 0;
    }
    *(uint32_t *)stack = STACK_CANARY;
    tss_slot = tss_esp0_slot();
    {
        /* See thread_create: low-to-high is regs, eflags, segs, ret. */
        uint32_t *sp = (uint32_t *)(stack + THREAD_STACK_SIZE);
        *--sp = (uint32_t)tss_slot;
        *--sp = stack + THREAD_STACK_SIZE;
        *--sp = uesp;
        *--sp = eip;
        *--sp = p->cr3;
        *--sp = (uint32_t)&user_entry_trampoline;
        for (int k = 0; k < 8; k++) {
            *--sp = 0;
        }
        *--sp = 0x202u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        *--sp = 0x10u;
        t->sp = sp;
    }
    t->id = g_next_id++;
    t->state = THREAD_READY;
    t->stack_base = stack;
    t->slice_used = 0;
    t->prio = (prio >= 32u) ? 31u : prio;
    t->is_user = 1;
    t->next = 0;
    t->pnext = 0;
    t->proc = p;
    t->user_stack = ustack_base;
    t->user_shared = shared_phys;
    process_attach(p, t);
    sched_add(t);
    g_thread_count++;
    return t;
}

__attribute__((noreturn)) void thread_exit(int code) {
    struct thread *t;
    struct thread *next;
    (void)code;
    __asm__ volatile("cli" ::: "memory");
    t = thread_current();
    if (t == 0 || t->stack_base == 0) {
        nova_panic("thread-exit-main");
    }
    thread_zombie_handoff(t);
    next = sched_pick(t);
    if (next == 0 || next == t) {
        nova_panic("sched-empty");
    }
    sched_switch_to(t, next);
    nova_panic("thread-exit-returned");
}

struct thread *thread_current(void) {
    return sched_current_thread();
}

uint32_t thread_count(void) {
    return g_thread_count;
}

void thread_count_inc(void) {
    g_thread_count++;
}
