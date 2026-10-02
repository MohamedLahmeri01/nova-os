/* NOVA OS virtual memory manager interface (Phase 2b).
 * PAE identity map with NX-by-default: only kernel .text executes.
 * The upper 3GB (PDPT entries 1..3 beyond the arena) stay unmapped:
 * reserved for the future user half + MMIO, not silently usable.
 */
#ifndef NOVA_VMM_H
#define NOVA_VMM_H

int vmm_init(void);
int vmm_selftest(void);
/* Map one MMIO page identity (P|RW|PWT|PCD). For LAPIC now, general
 * device MMIO later. Panics on PMM exhaustion. */
void vmm_map_mmio(uint32_t phys);
/* Page query for syscall validation (Phase 5). 0 ok (outputs set),
 * nonzero if the address is outside the managed map. */
int mem_page_state(uint32_t addr, int *present, int *user, int *writable);

#endif /* NOVA_VMM_H */
