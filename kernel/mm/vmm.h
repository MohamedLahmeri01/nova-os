/* NOVA OS virtual memory manager interface (Phase 2b, W^X 0.3-C).
 * Classic 32-bit non-PAE identity map: kernel .text read-only,
 * everything else RW-supervisor; no NX bit exists in this mode.
 * The gap [map_end, 2GB) stays unmapped: reserved for the future
 * user half + MMIO, not silently usable.
 */
#ifndef NOVA_VMM_H
#define NOVA_VMM_H

int vmm_init(void);
int vmm_selftest(void);
/* User-half permission audit (needs the heap: call after
 * kheap_init). Prints MEMMAP-OK. Nonzero on violation. */
int vmm_audit_user(void);
/* Map one MMIO page identity (P|RW|PWT|PCD). For LAPIC now, general
 * device MMIO later. Panics on PMM exhaustion. */
void vmm_map_mmio(uint32_t phys);
/* Kernel page directory (for brief CR3 windows over MMIO that user
 * address spaces must never map: CPL3 could otherwise poke devices
 * directly). Switch back immediately; cli-guarded by callers. */
uint32_t vmm_kernel_pd(void);
/* Page query for syscall validation (Phase 5). 0 ok (outputs set),
 * nonzero if the address is outside the managed map. */
int mem_page_state(uint32_t addr, int *present, int *user, int *writable);

#endif /* NOVA_VMM_H */
