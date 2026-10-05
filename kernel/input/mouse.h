/* NOVA OS PS/2 mouse interface (Phase 12e, input).
 * Standard 3-byte packets via IRQ12: signed motion (clamped to the
 * 320x200 screen), three buttons, left-edge click focuses the
 * topmost window under the cursor. Reset + ID proof at init
 * (MOUSE-OK); motion needs real hardware movement (parser and
 * click path execute in selftest via crafted bytes).
 */
#ifndef NOVA_MOUSE_H
#define NOVA_MOUSE_H

#include <stdint.h>

/* Enable aux port, reset the device (ACK + self-test + ID),
 * enable reporting, unmask IRQ12. Prints MOUSE-INIT on success,
 * MOUSE-ABSENT (success, dormant) when the controller is deaf.
 * Later reset/enable failures are -ENODEV (a talking controller
 * with a broken mouse: fatal). */
int mouse_init(void);
/* IRQ12: one aux byte. Never blocks, never prints. */
void mouse_irq(void);
/* Feed one packet byte (the IRQ path and the selftest share it). */
void mouse_feed(uint8_t b);
/* Cursor position (clamped) + button state (bit0 left). */
void mouse_getpos(uint32_t *x, uint32_t *y);
uint32_t mouse_buttons(void);
/* Nonzero when reset proof passed (cursor drawn, IRQ live). */
int mouse_present(void);
/* Pure packet decode: 9-bit signed motion + buttons. 0, or
 * -EINVAL (desync: bit3 clear) / -EIO (overflow: dropped). */
int mouse_parse_pkt(uint8_t b0, uint8_t b1, uint8_t b2,
                    int32_t *dx, int32_t *dy, uint32_t *btn);
/* Deterministic selftest (parser vectors, clamp, click-to-focus
 * on a scratch window). Prints MOUSE-OK. Nonzero on failure. */
int mouse_selftest(void);

#endif /* NOVA_MOUSE_H */
