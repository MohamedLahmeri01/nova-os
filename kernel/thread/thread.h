/* NOVA OS thread interface (Phase 4a).
 * Kernel threads, single address space (shared PD; per-process address
 * spaces arrive with user mode). Stacks are 8KB PMM-contiguous runs
 * with a canary word checked on every switch-in. thread_exit never
 * returns; reclamation happens via a graveyard drained on the next
 * switch/exit (safe: the dying stack is never freed under itself).
 */
#ifndef NOVA_THREAD_H
#define NOVA_THREAD_H

#include <stdint.h>

#define THREAD_READY 0
#define THREAD_RUNNING 1
#define THREAD_ZOMBIE 2

#define THREAD_STACK_FRAMES 2u
#define THREAD_STACK_SIZE (THREAD_STACK_FRAMES * 4096u)
#define THREAD_MAX 16u
#define STACK_CANARY 0xDEADBEEFu

struct process;

typedef void (*thread_fn_t)(void *arg);

struct thread {
    uint32_t *sp;
    uint32_t id;
    int state;
    uint32_t stack_base; /* 0 for the boot thread (not PMM-owned) */
    uint32_t slice_used;
    uint8_t prio; /* 0..31, higher preempts (strict); RR within level */
    uint8_t is_user; /* scheduled user thread (trampoline entry) */
    struct thread *next; /* runqueue link (policy-owned) */
    struct thread *pnext; /* process member link (process-owned) */
    struct process *proc;
    /* User-thread owned frames (freed on 0x81 exit): */
    uint32_t user_stack;
    uint32_t user_shared;
};

struct thread *thread_create(struct process *p, thread_fn_t fn, void *arg,
                             uint8_t prio);
struct thread *thread_create_user(struct process *p, uint32_t eip,
                                  uint32_t uesp, uint32_t ustack_base,
                                  uint32_t shared_phys, uint8_t prio);
__attribute__((noreturn)) void thread_exit(int code);
struct thread *thread_current(void);
uint32_t thread_count(void);
void thread_count_inc(void); /* boot-thread adoption accounting */
void thread_reap_graveyard(void);
void thread_zombie_handoff(struct thread *t);

/* Lowest-level primitive (kernel/ctx.S). Saves caller regs+eflags to
 * *old_sp, restores *new_sp, returns into the new context. */
void ctx_switch(uint32_t **old_sp, uint32_t **new_sp);

#endif /* NOVA_THREAD_H */
