/* NOVA OS timer interface (Phase 3).
 * PIT channel 0 at 100Hz drives g_ticks; timer_init enables delivery
 * (sti) and proves liveness by observing ticks (panics on timer-dead).
 */
#ifndef NOVA_TIME_H
#define NOVA_TIME_H

#include <stdint.h>

extern volatile uint32_t g_ticks;

void timer_init(void);

#endif /* NOVA_TIME_H */
