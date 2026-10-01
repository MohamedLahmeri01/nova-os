/* NOVA OS minimal process container (Phase 4a).
 * Pids from a counter; threads prepended to the member list (the
 * scheduler runqueue is independent and policy-owned).
 */
#include <stdint.h>
#include <stddef.h>

#include "mm/heap.h"
#include "thread/thread.h"
#include "process/process.h"

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
    return p;
}

void process_attach(struct process *p, struct thread *t) {
    if (p == 0 || t == 0) {
        return;
    }
    t->pnext = p->threads;
    p->threads = t;
}
