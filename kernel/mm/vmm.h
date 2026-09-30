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

#endif /* NOVA_VMM_H */
