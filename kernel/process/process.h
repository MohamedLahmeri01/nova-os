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

struct fs_node;

/* Open-file table per process (Phase 7): fds 0..NOVA_NFD-1, NULL
 * when free. Fixed size, no sharing yet (no fork/dup). */
#define NOVA_NFD 16u

/* SYS_CRED ops (Phase 10c): observe uid/gid, or voluntarily set
 * uid (root-to-any, self-to-self; nothing else). */
#define CRED_OP_GETUID 0u
#define CRED_OP_GETGID 1u
#define CRED_OP_SETUID 2u

struct process {
    uint32_t pid;
    uint32_t cr3; /* reserved: per-process page directory (user mode) */
    struct thread *threads;
    struct process *next;
    struct fs_file *fds[NOVA_NFD];
    /* Phase 10a credentials: capability mask (see process/caps.h)
     * + uid (0 = root). Default-deny: process_create zeroes both
     * (nobody, nothing); spawn copies the parent's, init is granted
     * explicitly at adoption. */
    uint32_t caps;
    uint32_t uid;
    /* Phase 10c group identity: single gid (no membership lists
     * yet); default nobody, root for init, inherited at spawn. No
     * setter in v0.1 (only uid switches via SYS_CRED). */
    uint32_t gid;
    /* Phase 10d jail root: NULL views the whole namespace;
     * otherwise absolute paths resolve under this node (floor for
     * "..", grafts invisible). Self-restriction: set once inward
     * via SYS_CHROOT, inherited at spawn, never cleared. */
    struct fs_node *jail_root;
    /* Phase 10b ceilings (see process/rlimit.h): fd count, file
     * size, mapped frames. Defaults are the hard ceilings (no
     * behavior change until lowered); spawn copies the parent's. */
    uint32_t rlimit_nofd;
    uint32_t rlimit_fsize;
    uint32_t rlimit_frames;
    /* Phase 13b CPU accounting: ticks consumed while running
     * (any thread of this process). kill at rlimit_ticks (below)
     * in sched_do_switch; UINT32_MAX default = unbounded. */
    uint32_t ticks_used;
    uint32_t rlimit_ticks;
};

extern struct process *g_init_process;

struct process *process_create(void);
void process_attach(struct process *p, struct thread *t);
/* Spawn a program image from FS bytes (Phase 7b exec, pm_main.c).
 * Returns the child pid, or negative -errno (never panics). */
int32_t spawn_file(const char *path);

#endif /* NOVA_PROCESS_H */
