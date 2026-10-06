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

#include "nova/types.h"

#define COMP_MAXWIN 8u
#define COMP_TITLE_H 12u
/* Desktop bar (Phase 12d): persistent top strip, painted last
 * (always on top). Shows "NOVA OS" + the focused title. */
#define COMP_BAR_H 12u

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
/* Set the title (Phase 12c: up to 15 chars, black on the title
 * bar). EINVAL on unused id. */
int comp_title(uint32_t id, const char *s);
/* Snapshot live windows into out (up to max; returns the live
 * count even past max, so (NULL, 0) probes the count). */
int comp_stat(struct nova_winstat *out, uint32_t max);
/* Topmost live window containing (x,y), or -1 (the click target;
 * menu bar clicks hit nothing: chrome is not a window). */
int comp_hit(uint32_t x, uint32_t y);
/* Window geometry for relative drawing (toolkit). EINVAL on
 * unused id. */
int comp_geom(uint32_t id, uint32_t *x, uint32_t *y, uint32_t *w,
              uint32_t *h);
/* Deterministic dispatch proof (guest selftest over crafted
 * frames + direct calls): empty focus, bad memory, bad ids,
 * count probe, positive focus/stat/close. Prints WIN-OK.
 * Nonzero on failure (fatal: panic). */
int win_selftest(void);
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
