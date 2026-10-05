/* NOVA OS compositor host test (Phase 12b, T19).
 * Host-side: the REAL kernel/gui/comp.c against a stub VRAM
 * (byte array behind the fb_* API) + stub serial/ticks. Layer
 * order, focus cycle, close/reuse, clipping, plus the guest
 * selftest itself (COMP-OK). The timed demo never runs on host
 * (ticks stand still). VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "drivers/fb.h"
#include "gui/comp.h"
#include "gui/font.h"
#include "syscall/syscall.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* --- kernel dep stubs --- */

__attribute__((noreturn)) void nova_panic(const char *reason) {
    printf("PANIC: %s\n", reason);
    exit(2);
}

void serial_puts(const char *s) {
    fputs(s, stdout);
}

void serial_putc(char c) {
    putchar(c);
}

void serial_putdec32(uint32_t v) {
    printf("%u", v);
}

void serial_puthex32(uint32_t v) {
    printf("%08x", v);
}

volatile uint32_t g_ticks = 1000u;

/* Stub VRAM: faithful byte array (clipping included). */
static uint8_t g_vram[FB_W * FB_H];

void fb_init(void) {
    memset(g_vram, 0, sizeof(g_vram));
}

void fb_clear(uint8_t color) {
    memset(g_vram, color, sizeof(g_vram));
}

void fb_pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x < FB_W && y < FB_H) {
        g_vram[y * FB_W + x] = color;
    }
}

uint8_t fb_get(uint32_t x, uint32_t y) {
    if (x < FB_W && y < FB_H) {
        return g_vram[y * FB_W + x];
    }
    return 0;
}

void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
             uint8_t color) {
    uint32_t xe = x + w;
    uint32_t ye = y + h;
    uint32_t xx, yy;
    if (xe > FB_W) {
        xe = FB_W;
    }
    if (ye > FB_H) {
        ye = FB_H;
    }
    for (yy = y; yy < ye; yy++) {
        for (xx = x; xx < xe; xx++) {
            g_vram[yy * FB_W + xx] = color;
        }
    }
}

void fb_splash(void) {
}

/* Text stubs mirror the driver blitter (reviewed same); the real
 * driver executes in-guest (FB-OK). Synthetic font below makes
 * every pixel exact. */
void fb_char(uint32_t x, uint32_t y, char c, uint8_t color) {
    const uint8_t *g = font_row((uint8_t)c);
    uint32_t r, k;
    for (r = 0; r < 8u; r++) {
        for (k = 0; k < 8u; k++) {
            if (((g[r] >> (7u - k)) & 1u) != 0) {
                fb_pixel(x + k, y + r, color);
            }
        }
    }
}

void fb_text(uint32_t x, uint32_t y, const char *s, uint8_t color) {
    uint32_t i;
    if (s == 0) {
        return;
    }
    for (i = 0; i < 64u && s[i] != 0; i++) {
        fb_char(x + i * 8u, y, s[i], color);
    }
}

int main(void) {
    int a, b;

    /* Layering through pixels: higher id wins overlaps. */
    a = comp_open(0, 0, 100, 100, FB_RED);
    b = comp_open(50, 50, 100, 100, FB_BLUE);
    check(a == 0 && b == 1, "ids", (unsigned long)a,
          (unsigned long)b);
    comp_present();
    check(fb_get(10, 50) == FB_RED, "back", fb_get(10, 50), 0);
    check(fb_get(75, 75) == FB_BLUE, "front", fb_get(75, 75),
          0);
    check(fb_get(10, 60) == FB_RED, "back-tall", fb_get(10, 60),
          0);
    /* Focus border: white 2px on the focused window edge. */
    check(fb_get(0, 0) == FB_WHITE, "focus-border",
          fb_get(0, 0), 0);
    comp_focus(1);
    comp_present();
    check(fb_get(0, 0) == FB_GRAY, "unfocus-border",
          fb_get(0, 0), 0);
    check(fb_get(50, 50) == FB_WHITE, "refocus-border",
          fb_get(50, 50), 0);
    /* Close one: background shows through. */
    check(comp_close(0) == 0, "close", 0, 0);
    comp_present();
    check(fb_get(10, 10) == FB_DARKGRAY, "bg-through",
          fb_get(10, 10), 0);
    check(comp_close(1) == 0, "close2", 0, 0);

    /* Text through a synthetic font ('X' diagonals only): title
     * placement + clipping exact, out-of-range chars map to '?'. */
    {
        static uint8_t xfont[128][8];
        uint32_t r, k;
        for (r = 0; r < 128u; r++) {
            for (k = 0; k < 8u; k++) {
                xfont[r][k] = 0;
            }
        }
        for (r = 0; r < 8u; r++) {
            xfont['X'][r] = (uint8_t)((0x81u >> r) | (0x81u << r));
            xfont['?'][r] = 0xFFu;
        }
        font_load(&xfont[0][0]);
        check(font_is_fallback() == 0, "font-load", 0, 0);
        fb_clear(FB_BLACK);
        fb_text(0, 0, "X", FB_WHITE);
        check(fb_get(0, 0) == FB_WHITE, "x-tl", fb_get(0, 0), 0);
        check(fb_get(7, 7) == FB_WHITE, "x-br", fb_get(7, 7), 0);
        check(fb_get(3, 3) == FB_WHITE, "x-mid", fb_get(3, 3), 0);
        check(fb_get(1, 0) == FB_BLACK, "x-off", fb_get(1, 0), 0);
        fb_text(8, 0, "\x01", FB_WHITE);
        check(fb_get(8, 0) == FB_WHITE, "x-quest", fb_get(8, 0),
              0);
        check(comp_title(0, "toolongtitle123456") == -NOVA_EINVAL,
              "title-shut", 0, 0);
    }

    /* Guest selftest runs the same stack in-guest (COMP-OK). */
    check(comp_selftest() == 0, "selftest", 0, 0);

    if (g_failed) {
        printf("VERDICT: compositor BROKEN\n");
        return 1;
    }
    printf("VERDICT: compositor correct\n");
    return 0;
}
