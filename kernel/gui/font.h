/* NOVA OS 8x8 font interface (Phase 12c, BIOS-sourced).
 * Glyphs come from the VGA BIOS 8x8 font (INT 43h vector), copied
 * from wherever it lives (< 1MB, readable) at init. Validated by
 * structure (blank space, sane ink coverage); on failure every
 * glyph becomes an outline box and the source logs as fallback
 * (readable failure, never silent garbage). Chars outside 32..126
 * render as '?'. Host tests load synthetic fonts instead (ticks
 * and BIOS addresses don't exist there).
 */
#ifndef NOVA_FONT_H
#define NOVA_FONT_H

#include <stdint.h>

#define FONT_GLYPHS 128u
#define FONT_ROWS 8u

/* Copy the BIOS font + validate (logs FONT-SRC bios/fallback). */
void font_init(void);
/* Load 1024 bytes as the font (test seam; skips validation). */
void font_load(const uint8_t *src);
/* Row pointer for a char (8 bytes, '?' fallback outside 32..126). */
const uint8_t *font_row(uint8_t c);
/* Nonzero when the BIOS copy failed validation (boxes in use). */
int font_is_fallback(void);

#endif /* NOVA_FONT_H */
