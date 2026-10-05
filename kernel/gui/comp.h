/* NOVA OS compositor interface (Phase 12b, layers + focus).
 * Fixed pool of windows painted back-to-front (id order = z order)
 * onto the mode-13h framebuffer: title bar + fill + focus border.
 * Focus highlights only (no reorder yet); present repaints fully
 * (64KB stores: no damage tracking in v0.1). No text yet (font
 * arrives with the toolkit); windows are identified by color.
 * All functions return 0 or negative -errno (see syscall.h).
 */
#ifndef NOVA_COMP_H
#define NOVA_COMP_H

#include <stdint.h>

#define COMP_MAXWIN 8u
#define COMP_TITLE_H 12u

/* Open a window (clipped to the screen at paint). Returns the id
 * (lowest free slot) or negative -errno (EINVAL on empty/off
 * screen geometry, ENOSPC when full). */
int comp_open(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
              uint8_t color);
/* Close (frees the slot; focus falls to the lowest live id, else
 * none). EINVAL on unused id. */
int comp_close(uint32_t id);
/* Focus one window (highlight only). EINVAL on unused id. */
int comp_focus(uint32_t id);
/* Cycle focus over live windows (none if empty). Returns the new
 * focus id, or -1. */
int comp_focus_next(void);
/* Current focus id, or -1. */
int comp_focused(void);
/* Repaint everything (bg + windows back-to-front). */
void comp_present(void);
/* Timed demo scene (3 overlapping windows, focus cycle with
 * COMP-FOCUS markers). Guest only (tick delays). */
void comp_demo(void);
/* Deterministic selftest (open/close/focus/layers/pixels).
 * Prints COMP-OK. Nonzero on failure (fatal: panic). */
int comp_selftest(void);

#endif /* NOVA_COMP_H */
