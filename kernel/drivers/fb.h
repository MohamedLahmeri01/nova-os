/* NOVA OS framebuffer interface (Phase 12a, mode 13h).
 * 320x200x256 at 0xA0000 (byte-per-pixel, stride 320). stage1
 * selects the mode via BIOS; this driver owns pixels + a 16-entry
 * DAC palette. All coordinates clip silently (untrusted callers).
 * All functions return 0 or negative -errno (see syscall.h).
 */
#ifndef NOVA_FB_H
#define NOVA_FB_H

#include <stdint.h>

#define FB_W 320u
#define FB_H 200u

/* Palette indices (programmed at init, 6-bit DAC channels). */
#define FB_BLACK 0u
#define FB_WHITE 1u
#define FB_RED 2u
#define FB_GREEN 3u
#define FB_BLUE 4u
#define FB_YELLOW 5u
#define FB_CYAN 6u
#define FB_MAGENTA 7u
#define FB_GRAY 8u
#define FB_DARKGRAY 9u
#define FB_DARKRED 10u
#define FB_DARKGREEN 11u
#define FB_DARKBLUE 12u
#define FB_BROWN 13u
#define FB_TEAL 14u
#define FB_PURPLE 15u

/* Program the palette + clear to black. Idempotent. */
void fb_init(void);
/* Fill the screen. */
void fb_clear(uint8_t color);
/* One pixel (clipped). */
void fb_pixel(uint32_t x, uint32_t y, uint8_t color);
/* Read one pixel (out of range reads 0, never faults). */
uint8_t fb_get(uint32_t x, uint32_t y);
/* Filled rectangle, corner (x,y), size (w,h), clipped. */
void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
             uint8_t color);
/* One 8x8 glyph (BIOS font; out-of-range chars render '?'). */
void fb_char(uint32_t x, uint32_t y, char c, uint8_t color);
/* NUL-terminated string, 8px cells, transparent bg, capped at 64
 * chars (untrusted lengths stop here). */
void fb_text(uint32_t x, uint32_t y, const char *s, uint8_t color);
/* Boot splash (border + bars + blocks; the screenshot proof). */
void fb_splash(void);
/* Deterministic selftest (clear/readback/rect/clip/palette).
 * Prints FB-OK. Nonzero on failure (fatal: panic). */
int fb_selftest(void);

#endif /* NOVA_FB_H */
