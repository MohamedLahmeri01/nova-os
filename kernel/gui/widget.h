/* NOVA OS widget interface (Phase 14d toolkit, 14e-ii press
 * states). Labels + buttons drawn window-relative (clipped by
 * fb), stateless pixels: point-in-rect queries plus a
 * pressed-look renderer; press tracking lives with the caller
 * (no registry). All functions return 0 or negative -errno.
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
/* Button pressed look: inverted bevel (dark top/left, white
 * bottom/right), label shifted +1,+1. Same validation. */
int wid_button_down(uint32_t id, uint32_t dx, uint32_t dy,
                    uint32_t w, uint32_t h, const char *label);
/* Point-in-rect for a control drawn at (dx,dy,w,h): 1 inside,
 * 0 outside, -EINVAL on unused id. Edges match comp_hit
 * (left/top in, right/bottom out). */
int wid_hit(uint32_t id, uint32_t dx, uint32_t dy, uint32_t w,
            uint32_t h, uint32_t px, uint32_t py);
/* Deterministic selftest (placement, bevels up + down, hit
 * edges, discipline). Prints WID-OK + PRESS-OK. Nonzero on
 * failure (fatal: panic). */
int wid_selftest(void);

#endif /* NOVA_WIDGET_H */
