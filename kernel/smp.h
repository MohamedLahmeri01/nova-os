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

#endif /* NOVA_SMP_H */
