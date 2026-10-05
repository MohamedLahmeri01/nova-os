/* NOVA OS 8x8 font (Phase 12c).
 * The VGA BIOS keeps its text-mode font resident and publishes it
 * through INT 43h (offset:segment at 0x10C); the bytes live below
 * 1MB, which the VMM identity-maps readable (page 0 itself is
 * read-only, which is all we need). Copy + structural validation;
 * boxes on failure. No heap, no IRQ interaction (boot-time init).
 */
#include <stdint.h>

#include "serial.h"
#include "gui/font.h"

static uint8_t g_font[FONT_GLYPHS][FONT_ROWS];
static int g_fallback;

void font_load(const uint8_t *src) {
    if (src == 0) {
        return;
    }
    for (uint32_t i = 0; i < FONT_GLYPHS * FONT_ROWS; i++) {
        ((uint8_t *)g_font)[i] = src[i];
    }
    g_fallback = 0;
}

static void font_boxes(void) {
    for (uint32_t c = 0; c < FONT_GLYPHS; c++) {
        g_font[c][0] = 0xFFu;
        for (uint32_t r = 1; r + 1u < FONT_ROWS; r++) {
            g_font[c][r] = 0x81u;
        }
        g_font[c][FONT_ROWS - 1u] = 0xFFu;
    }
    g_fallback = 1;
}

void font_init(void) {
    uint32_t vec = *(volatile uint32_t *)0x10Cu;
    uint32_t phys =
        ((vec >> 16) & 0xFFFFu) * 16u + (vec & 0xFFFFu);
    uint32_t ink = 0;
    if (phys + FONT_GLYPHS * FONT_ROWS > 0x100000u) {
        font_boxes();
    } else {
        for (uint32_t i = 0; i < FONT_GLYPHS * FONT_ROWS; i++) {
            uint8_t b =
                *(volatile uint8_t *)(phys + i);
            ((uint8_t *)g_font)[i] = b;
            for (uint32_t k = 0; k < 8u; k++) {
                ink += (b >> k) & 1u;
            }
        }
        /* Structural check: blank space (char 32) + plausible
         * ink coverage (a real font is neither empty nor solid). */
        {
            int space_blank = 1;
            for (uint32_t r = 0; r < FONT_ROWS; r++) {
                if (g_font[32][r] != 0) {
                    space_blank = 0;
                }
            }
            if (!space_blank || ink < 1000u || ink > 5000u) {
                font_boxes();
            }
        }
    }
    serial_puts("FONT-SRC ");
    serial_puts(g_fallback ? "fallback\n" : "bios\n");
}

const uint8_t *font_row(uint8_t c) {
    if (c < 32u || c > 126u) {
        c = (uint8_t)'?';
    }
    return g_font[c];
}

int font_is_fallback(void) {
    return g_fallback;
}
