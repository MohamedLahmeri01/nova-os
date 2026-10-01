/* NOVA OS user-mode roundtrip test (Phase 4b).
 * Builds a throwaway address space (kernel low cloned, one user PT),
 * copies a self-contained probe to a user page, and enters ring 3 via
 * iret. The probe reports CS + magic through a shared page and returns
 * via INT 0x81 (DPL3 gate) -> CR3 restore -> longjmp home. All locals
 * used after the jump are assigned before ksetjmp (setjmp discipline).
 * Timer IRQs during user mode exercise TSS.ESP0 transparently.
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/addrspace.h"
#include "gdt.h"
#include "irq/irq.h"
#include "sched/sched.h"
#include "thread/thread.h"
#include "user.h"
#include "process/process.h"
#include "time/time.h"
#include "time/time.h"

#if defined(__i386__)
#include "arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "arch/x86_64/cpu.h"
#else
#error "user: no arch_cpu backend for this architecture"
#endif

#define USER_CODE 0x80000000u
#define USER_SHARED 0x80001000u
#define USER_STACK_TOP 0x80400000u
#define USER_MAGIC 0x55534552u

extern uint8_t user_probe_start[];
extern uint8_t user_probe_end[];
extern void user_ret_stub(void);
extern int ksetjmp(uint32_t *buf);
extern void klongjmp(uint32_t *buf, uint32_t val);

static uint32_t g_jmp[6];
static uint32_t g_saved_kpd;

void user_ret_handler(void) {
    struct thread *cur = sched_current_thread();
    if (cur != 0 && cur->is_user) {
        /* Scheduled user thread exit: free its user pages, hand off
         * the zombie, and switch away (never returns here). */
        struct thread *next;
        if (cur->user_stack != 0) {
            pmm_free_contig(cur->user_stack, 2);
        }
        if (cur->user_shared != 0) {
            pmm_free_frame(cur->user_shared);
        }
        thread_zombie_handoff(cur);
        next = sched_pick(cur);
        if (next == 0 || next == cur) {
            nova_panic("sched-empty");
        }
        sched_switch_to(cur, next);
        nova_panic("user-exit-returned");
    }
    arch_write_cr3(g_saved_kpd);
    klongjmp(g_jmp, 1);
    nova_panic("longjmp-returned");
}

int user_selftest(void) {
    struct process *p;
    struct addrspace *as;
    uint32_t code_f, shared_f, s_base, tss_stack;
    uint32_t probe_len;
    uint32_t user_esp;
    probe_len = (uint32_t)(user_probe_end - user_probe_start);
    gdt_init();
    p = process_create();
    as = addrspace_create();
    if (p == 0 || as == 0) {
        nova_panic("user-no-proc");
    }
    p->cr3 = as->pd;
    code_f = pmm_alloc_frame();
    shared_f = pmm_alloc_frame();
    s_base = pmm_alloc_contig(2);
    tss_stack = pmm_alloc_contig(2);
    if (code_f == 0 || shared_f == 0 || s_base == 0 || tss_stack == 0) {
        nova_panic("user-no-mem");
    }
    addrspace_map(as, USER_CODE, code_f, 1);
    addrspace_map(as, USER_SHARED, shared_f, 1);
    addrspace_map(as, USER_STACK_TOP - 8192u, s_base, 1);
    addrspace_map(as, USER_STACK_TOP - 4096u, s_base + 4096u, 1);
    for (uint32_t i = 0; i < probe_len; i++) {
        ((uint8_t *)code_f)[i] = user_probe_start[i];
    }
    *(uint32_t *)(shared_f) = 0;
    *(uint32_t *)(shared_f + 4u) = 0;
    *(uint32_t *)(s_base + 8192u - 4u) = USER_MAGIC;
    *(uint32_t *)(s_base + 8192u - 8u) = USER_SHARED;
    /* NOTE: the writes above use PHYSICAL addresses (identity map),
     * but the user ESP must be the VIRTUAL stack top. */
    user_esp = USER_STACK_TOP - 8u;
    idt_set_gate(0x81, user_ret_stub, 0xEEu);
    tss_set_esp0(tss_stack + 8192u);
    tss_load();
    g_saved_kpd = arch_read_cr3();
    if (ksetjmp(g_jmp) == 0) {
        arch_write_cr3(as->pd);
        __asm__ volatile("pushl $0x23; pushl %0; pushl $0x202; "
                         "pushl $0x1B; pushl %1; iret" ::"r"(user_esp),
                         "r"(USER_CODE)
                         : "memory");
        nova_panic("user-iret-returned");
    }
    /* Longjmp does not restore EFLAGS: the INT 0x81 gate cleared IF,
     * so re-enable here or the timer (and all preemption) stays dead. */
    __asm__ volatile("sti" ::: "memory");
    {
        uint32_t *sh = (uint32_t *)shared_f;
        if (sh[0] != 0x1Bu || sh[1] != USER_MAGIC) {
            nova_panic("user-verify-fail");
        }
    }
    serial_puts("USER cs=0x1b magic-ok\nUSER-OK\n");
    pmm_free_frame(code_f);
    pmm_free_frame(shared_f);
    pmm_free_contig(s_base, 2);
    pmm_free_contig(tss_stack, 2);
    addrspace_destroy(as);
    kfree(p);
    return 0;
}

/* --- scheduled user threads: overlap proof in ring 3 ---
 * One process/PD shared by two threads (own stacks + result pages,
 * shared spin-probe code). Progress counters sampled from the kernel
 * prove concurrent execution; magics + CS prove correctness. */

extern uint8_t user_spin_start[];
extern uint8_t user_spin_end[];

#define UTHREAD_MAGIC1 0xAAAAAAAAu
#define UTHREAD_MAGIC2 0xBBBBBBBBu
#define UTHREAD_FINAL 488281u
#define UTHREAD_STACK_TOP 0x80400000u

/* Observer state (set before spawn; observer runs at prio 31 so it
 * samples while user threads live, which starving main cannot). */
static uint32_t g_obs_sh1;
static uint32_t g_obs_sh2;
static volatile int g_obs_overlap;
static volatile int g_obs_done;

static void obs_thread(void *arg) {
    uint32_t t0;
    (void)arg;
    t0 = g_ticks;
    for (;;) {
        uint32_t *r1 = (uint32_t *)g_obs_sh1;
        uint32_t *r2 = (uint32_t *)g_obs_sh2;
        uint32_t p1 = r1[2];
        uint32_t p2 = r2[2];
        if (p1 > 0 && p1 < UTHREAD_FINAL && p2 > 0 && p2 < UTHREAD_FINAL) {
            g_obs_overlap = 1;
        }
        if ((r1[1] == UTHREAD_MAGIC1 && r2[1] == UTHREAD_MAGIC2) ||
            g_obs_overlap) {
            break;
        }
        if (g_ticks - t0 > 3000u) {
            break;
        }
        sched_yield();
    }
    g_obs_done = 1;
    thread_exit(0);
}

int user_sched_test(void) {
    struct process *p;
    struct addrspace *as;
    uint32_t code_f, s1, s2, sh1, sh2;
    uint32_t spin_len;
    uint32_t t0;
    extern struct process *g_init_process;
    p = process_create();
    as = addrspace_create();
    if (p == 0 || as == 0) {
        nova_panic("usched-no-proc");
    }
    p->cr3 = as->pd;
    code_f = pmm_alloc_frame();
    s1 = pmm_alloc_contig(2);
    s2 = pmm_alloc_contig(2);
    sh1 = pmm_alloc_frame();
    sh2 = pmm_alloc_frame();
    if (code_f == 0 || s1 == 0 || s2 == 0 || sh1 == 0 || sh2 == 0) {
        nova_panic("usched-no-mem");
    }
    addrspace_map(as, USER_CODE, code_f, 1);
    addrspace_map(as, USER_SHARED, sh1, 1);
    addrspace_map(as, USER_SHARED + 4096u, sh2, 1);
    addrspace_map(as, UTHREAD_STACK_TOP - 8192u, s1, 1);
    addrspace_map(as, UTHREAD_STACK_TOP - 4096u, s1 + 4096u, 1);
    addrspace_map(as, UTHREAD_STACK_TOP - 16384u, s2, 1);
    addrspace_map(as, UTHREAD_STACK_TOP - 12288u, s2 + 4096u, 1);
    spin_len = (uint32_t)(user_spin_end - user_spin_start);
    for (uint32_t i = 0; i < spin_len; i++) {
        ((uint8_t *)code_f)[i] = user_spin_start[i];
    }
    *(uint32_t *)sh1 = 0;
    *(uint32_t *)(sh1 + 4u) = 0;
    *(uint32_t *)(sh1 + 8u) = 0;
    *(uint32_t *)sh2 = 0;
    *(uint32_t *)(sh2 + 4u) = 0;
    *(uint32_t *)(sh2 + 8u) = 0;
    /* Per-thread stacks preloaded with [shared][magic] (probe ABI). */
    *(uint32_t *)(s1 + 8192u - 4u) = UTHREAD_MAGIC1;
    *(uint32_t *)(s1 + 8192u - 8u) = USER_SHARED;
    *(uint32_t *)(s2 + 8192u - 4u) = UTHREAD_MAGIC2;
    *(uint32_t *)(s2 + 8192u - 8u) = USER_SHARED + 4096u;
    if (thread_create_user(p, USER_CODE, UTHREAD_STACK_TOP - 8u, s1,
                           sh1, 16u) == 0 ||
        thread_create_user(p, USER_CODE, UTHREAD_STACK_TOP - 8192u - 8u,
                           s2, sh2, 16u) == 0) {
        nova_panic("usched-spawn-fail");
    }
    g_obs_sh1 = sh1;
    g_obs_sh2 = sh2;
    g_obs_overlap = 0;
    g_obs_done = 0;
    /* Observer shares the workers' level (16): strict priority has no
     * cross-level yield, so a higher-priority spinner would starve the
     * very threads it observes. RR within the level shares fairly. */
    if (thread_create(g_init_process, obs_thread, 0, 16u) == 0) {
        nova_panic("usched-obs-fail");
    }
    serial_puts("USERSCHED threads=2\n");
    t0 = g_ticks;
    for (;;) {
        /* NOTE: main (prio 0) starves while user threads (16) live, so
         * overlap must be observed by a HIGHER-priority observer thread
         * (see obs_thread below), not here. This loop only waits. */
        uint32_t *r1 = (uint32_t *)sh1;
        uint32_t *r2 = (uint32_t *)sh2;
        uint32_t done = (r1[1] == UTHREAD_MAGIC1) &&
                        (r2[1] == UTHREAD_MAGIC2);
        if (done && g_obs_done) {
            break;
        }
        if (g_ticks - t0 > 3000u) {
            nova_panic("usched-stuck");
        }
        __asm__ volatile("hlt");
    }
    if (!g_obs_overlap) {
        nova_panic("usched-no-overlap");
    }
    {
        uint32_t *r1 = (uint32_t *)sh1;
        uint32_t *r2 = (uint32_t *)sh2;
        if (r1[0] != 0x1Bu || r2[0] != 0x1Bu || r1[2] != UTHREAD_FINAL ||
            r2[2] != UTHREAD_FINAL) {
            nova_panic("usched-verify-fail");
        }
    }
    serial_puts("USER-SCHED-OK\n");
    pmm_free_frame(code_f);
    pmm_free_frame(sh1);
    pmm_free_frame(sh2);
    pmm_free_contig(s1, 2);
    pmm_free_contig(s2, 2);
    addrspace_destroy(as);
    kfree(p);
    return 0;
}
