/* NOVA OS minimal process container (Phase 4a).
 * Pids from a counter; threads prepended to the member list (the
 * scheduler runqueue is independent and policy-owned).
 */
#include <stdint.h>
#include <stddef.h>

#include "mm/heap.h"
#include "thread/thread.h"
#include "process/process.h"
#include "process/caps.h"
#include "process/rlimit.h"

struct process *g_init_process;

static uint32_t g_next_pid = 1;

struct process *process_create(void) {
    struct process *p = (struct process *)kmalloc(sizeof(struct process));
    if (p == 0) {
        return 0;
    }
    p->pid = g_next_pid++;
    p->cr3 = 0;
    p->threads = 0;
    p->next = 0;
    /* kmalloc does not zero: every fd starts free. */
    for (uint32_t i = 0; i < NOVA_NFD; i++) {
        p->fds[i] = 0;
    }
    /* Default-deny credentials (Phase 10a): spawn copies the
     * parent's over these; init is granted at adoption. */
    p->caps = 0;
    p->uid = UID_NOBODY;
    /* Open ceilings (Phase 10b): no behavior change until lowered;
     * spawn copies the parent's over these. */
    p->rlimit_nofd = RLIM_NOFD_DEF;
    p->rlimit_fsize = RLIM_FSIZE_DEF;
    p->rlimit_frames = RLIM_FRAMES_DEF;
    return p;
}

void process_attach(struct process *p, struct thread *t) {
    if (p == 0 || t == 0) {
        return;
    }
    t->pnext = p->threads;
    p->threads = t;
}
