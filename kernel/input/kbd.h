/* NOVA OS keyboard interface (Phase 6, blocking getkey 16-i).
 * PS/2 Set-1 scancodes -> ASCII ring buffer. BIOS leaves the controller
 * in defaults (scan set 1); no controller programming here yet.
 * getkey blocks when scheduled (IRQ push wakes one waiter);
 * -EAGAIN only pre-scheduler, -EBUSY on a second parker.
 */
#ifndef NOVA_KBD_H
#define NOVA_KBD_H

#include <stdint.h>

void kbd_init(void);
void kbd_irq(void);
/* One scancode through the translator (shared by the IRQ port
 * read and deterministic selftests). */
void kbd_feed(uint8_t sc);
int32_t kbd_getkey(void);
/* Blocking proof (scheduler live): parked reader woken by fed
 * scancodes, second-parker EBUSY. Prints KBDBLK-OK. Nonzero on
 * failure (fatal: panic). */
int kbd_block_selftest(void);

#endif /* NOVA_KBD_H */
