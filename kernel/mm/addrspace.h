/* NOVA OS address space interface (Phase 4b).
 * One PD per process: kernel low mappings cloned (shared frames, never
 * freed per-PD), user half private. Only the user half (PD >= 512,
 * i.e. >= 2GB) may be mapped through this API; kernel mappings are
 * fixed at clone time (protects kernel integrity by construction).
 */
#ifndef NOVA_ADDRSPACE_H
#define NOVA_ADDRSPACE_H

#include <stdint.h>

#define USER_BASE 0x80000000u

struct addrspace {
    uint32_t pd; /* physical address of the page directory */
    /* Phase 10b frame accounting: successful user maps so far vs
     * the process ceiling (spawn copies the parent's limit here).
     * No unmap API: the count only grows with the space. */
    uint32_t mapped;
    uint32_t max_mapped;
};

struct addrspace *addrspace_create(void);
void addrspace_destroy(struct addrspace *as);
/* Map one page (user half only). 0 or -ENOSPC at the ceiling;
 * malformed arguments still panic (programmer bug, not policy). */
int addrspace_map(struct addrspace *as, uint32_t virt, uint32_t phys,
                  int writable);

#endif /* NOVA_ADDRSPACE_H */
