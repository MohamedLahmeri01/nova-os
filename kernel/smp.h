/* NOVA OS SMP interface (Phase 4c).
 * MP-table enumeration + AP bring-up (INIT/SIPI) + park. APs execute
 * a self-check (record ID, print, park halted with IF=0). No AP
 * scheduling yet (single runqueue on BSP; balancing deferred).
 */
#ifndef NOVA_SMP_H
#define NOVA_SMP_H

#include <stdint.h>

void smp_boot(void);
uint32_t smp_cpu_count(void);
/* This CPU's LAPIC ID (hardware read, not a constant). Valid
 * after smp_boot (LAPIC base resolved). All callers today are
 * the BSP (APs parked); per-CPU design lives in ADR-0069. */
uint32_t smp_cpu_id(void);
/* MP-table BSP ID, when the table named one: 0 found + id out,
 * nonzero when unknown (no-MP-table fallback path). */
int smp_bsp_id(uint32_t *out);

#endif /* NOVA_SMP_H */
