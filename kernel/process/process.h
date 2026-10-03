/* NOVA OS process interface (Phase 4a, minimal container).
 * A process is an ownership scope: pid + member threads. All processes
 * share the kernel page directory for now; per-process address spaces
 * arrive with user mode (the PD field is reserved for it).
 */
#ifndef NOVA_PROCESS_H
#define NOVA_PROCESS_H

#include <stdint.h>

struct thread;

struct fs_file;

/* Open-file table per process (Phase 7): fds 0..NOVA_NFD-1, NULL
 * when free. Fixed size, no sharing yet (no fork/dup). */
#define NOVA_NFD 16u

struct process {
    uint32_t pid;
    uint32_t cr3; /* reserved: per-process page directory (user mode) */
    struct thread *threads;
    struct process *next;
    struct fs_file *fds[NOVA_NFD];
};

extern struct process *g_init_process;

struct process *process_create(void);
void process_attach(struct process *p, struct thread *t);
/* Spawn a program image from FS bytes (Phase 7b exec, pm_main.c).
 * Returns the child pid, or negative -errno (never panics). */
int32_t spawn_file(const char *path);

#endif /* NOVA_PROCESS_H */
