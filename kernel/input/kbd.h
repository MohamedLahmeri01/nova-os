/* NOVA OS keyboard interface (Phase 6).
 * PS/2 Set-1 scancodes -> ASCII ring buffer. BIOS leaves the controller
 * in defaults (scan set 1); no controller programming here yet.
 */
#ifndef NOVA_KBD_H
#define NOVA_KBD_H

#include <stdint.h>

void kbd_init(void);
void kbd_irq(void);
int32_t kbd_getkey(void);

#endif /* NOVA_KBD_H */
