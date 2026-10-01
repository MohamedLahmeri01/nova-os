/* NOVA OS process interface (Phase 4a, minimal container).
 * A process is an ownership scope: pid + member threads. All processes
 * share the kernel page directory for now; per-process address spaces
 * arrive with user mode (the PD field is reserved for it).
 */
#ifndef NOVA_PROCESS_H
#define NOVA_PROCESS_H

#include <stdint.h>

struct thread;

struct process {
    uint32_t pid;
    uint32_t cr3; /* reserved: per-process page directory (user mode) */
    struct thread *threads;
    struct process *next;
};

extern struct process *g_init_process;

struct process *process_create(void);
void process_attach(struct process *p, struct thread *t);

#endif /* NOVA_PROCESS_H */
