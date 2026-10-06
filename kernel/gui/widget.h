/* NOVA OS widget interface (Phase 14d, rendering only).
 * Labels + buttons drawn window-relative (clipped by fb).
 * No events yet (no pointer to click with, no keyboard focus
 * protocol): widgets are drawn state, honestly documented.
 * Interaction (hit-testing, press states, keyboard nav) waits
 * for input. All functions return 0 or negative -errno.
 */
#ifndef NOVA_WIDGET_H
#define NOVA_WIDGET_H

#include <stdint.h>

/* Text label at (dx,dy) in the window, transparent bg. */
int wid_label(uint32_t id, uint32_t dx, uint32_t dy, const char *s,
              uint8_t color);
/* Beveled button (gray face, white/dark edges, centered black
 * label). Minimum 16x14 (fits an 8px glyph + padding). */
int wid_button(uint32_t id, uint32_t dx, uint32_t dy, uint32_t w,
               uint32_t h, const char *label);
/* Deterministic selftest (placement, bevels, discipline).
 * Prints WID-OK. Nonzero on failure (fatal: panic). */
int wid_selftest(void);

#endif /* NOVA_WIDGET_H */
