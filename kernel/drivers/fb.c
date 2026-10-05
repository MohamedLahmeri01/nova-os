/* NOVA OS mode-13h framebuffer (Phase 12a).
 * Byte VRAM at 0xA0000 (identity-mapped RW by the VMM low map).
 * DAC palette via ports 0x3C8 (index-write) / 0x3C9 (channels) /
 * 0x3C7 (index-read). Single-core + IRQ-safe by construction:
 * every op is a bounded run of stores (no shared state at all).
 */
#include <stdint.h>

#include "serial.h"
#include "drivers/fb.h"
#include "gui/font.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#else
#error "fb: no arch backend for this architecture"
#endif

#define FB_BASE ((volatile uint8_t *)0xA0000u)
#define DAC_WIDX 0x3C8u
#define DAC_RIDX 0x3C7u
#define DAC_DATA 0x3C9u

/* 16 x (r,g,b), 6-bit channels. */
static const uint8_t g_pal[16][3] = {
    { 0, 0, 0 },     /* black */
    { 63, 63, 63 },  /* white */
    { 63, 0, 0 },    /* red */
    { 0, 63, 0 },    /* green */
    { 0, 0, 63 },    /* blue */
    { 63, 63, 0 },   /* yellow */
    { 0, 63, 63 },   /* cyan */
    { 63, 0, 63 },   /* magenta */
    { 42, 42, 42 },  /* gray */
    { 21, 21, 21 },  /* dark gray */
    { 42, 0, 0 },    /* dark red */
    { 0, 42, 0 },    /* dark green */
    { 0, 0, 42 },    /* dark blue */
    { 42, 21, 0 },   /* brown */
    { 0, 42, 42 },   /* teal */
    { 42, 0, 42 },   /* purple */
};

static void pal_set(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    arch_outb(DAC_WIDX, idx);
    arch_outb(DAC_DATA, r);
    arch_outb(DAC_DATA, g);
    arch_outb(DAC_DATA, b);
}

static void pal_get(uint8_t idx, uint8_t *r, uint8_t *g,
                    uint8_t *b) {
    arch_outb(DAC_RIDX, idx);
    *r = arch_inb(DAC_DATA);
    *g = arch_inb(DAC_DATA);
    *b = arch_inb(DAC_DATA);
}

void fb_init(void) {
    for (uint32_t i = 0; i < 16u; i++) {
        pal_set((uint8_t)i, g_pal[i][0], g_pal[i][1],
                g_pal[i][2]);
    }
    fb_clear(FB_BLACK);
}

void fb_clear(uint8_t color) {
    for (uint32_t i = 0; i < FB_W * FB_H; i++) {
        FB_BASE[i] = color;
    }
}

void fb_pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x >= FB_W || y >= FB_H) {
        return;
    }
    FB_BASE[y * FB_W + x] = color;
}

uint8_t fb_get(uint32_t x, uint32_t y) {
    if (x >= FB_W || y >= FB_H) {
        return 0;
    }
    return FB_BASE[y * FB_W + x];
}

void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
             uint8_t color) {    uint32_t xe = x + w;
    uint32_t ye = y + h;
    if (xe < x) {
        xe = 0xFFFFFFFFu; /* wrapped: clamp to edge below */
    }
    if (ye < y) {
        ye = 0xFFFFFFFFu;
    }
    if (xe > FB_W) {
        xe = FB_W;
    }
    if (ye > FB_H) {
        ye = FB_H;
    }
    for (uint32_t yy = y; yy < ye; yy++) {
        for (uint32_t xx = x; xx < xe; xx++) {
            FB_BASE[yy * FB_W + xx] = color;
        }
    }
}

void fb_char(uint32_t x, uint32_t y, char c, uint8_t color) {
    const uint8_t *g = font_row((uint8_t)c);
    for (uint32_t r = 0; r < 8u; r++) {
        uint8_t bits = g[r];
        for (uint32_t k = 0; k < 8u; k++) {
            if (((bits >> (7u - k)) & 1u) != 0) {
                fb_pixel(x + k, y + r, color);
            }
        }
    }
}

void fb_text(uint32_t x, uint32_t y, const char *s, uint8_t color) {
    if (s == 0) {
        return;
    }
    for (uint32_t i = 0; i < 64u && s[i] != 0; i++) {
        fb_char(x + i * 8u, y, s[i], color);
    }
}

void fb_splash(void) {
    fb_clear(FB_DARKBLUE);
    /* White frame. */
    fb_rect(8, 8, FB_W - 16u, 3, FB_WHITE);
    fb_rect(8, FB_H - 11u, FB_W - 16u, 3, FB_WHITE);
    fb_rect(8, 8, 3, FB_H - 16u, FB_WHITE);
    fb_rect(FB_W - 11u, 8, 3, FB_H - 16u, FB_WHITE);
    /* Color bars. */
    fb_rect(32, 40, 256, 20, FB_RED);
    fb_rect(32, 64, 256, 20, FB_GREEN);
    fb_rect(32, 88, 256, 20, FB_BLUE);
    /* Center blocks: white / yellow / cyan. */
    fb_rect(48, 124, 64, 40, FB_WHITE);
    fb_rect(128, 124, 64, 40, FB_YELLOW);
    fb_rect(208, 124, 64, 40, FB_CYAN);
}

int fb_selftest(void) {
    uint8_t r, g, b;
    fb_init();
    /* Cleared black. */
    if (fb_get(0, 0) != FB_BLACK ||
        fb_get(FB_W - 1u, FB_H - 1u) != FB_BLACK) {
        serial_puts("FB-FAIL clear\n");
        return -1;
    }
    /* Write/readback incl. far corner. */
    fb_pixel(0, 0, FB_WHITE);
    fb_pixel(FB_W - 1u, FB_H - 1u, FB_RED);
    fb_pixel(160, 100, FB_GREEN);
    if (fb_get(0, 0) != FB_WHITE ||
        fb_get(FB_W - 1u, FB_H - 1u) != FB_RED ||
        fb_get(160, 100) != FB_GREEN) {
        serial_puts("FB-FAIL pixel\n");
        return -1;
    }
    /* Out of range: no crash, in-range untouched. */
    fb_pixel(FB_W, 0, FB_BLUE);
    fb_pixel(0, FB_H, FB_BLUE);
    fb_pixel(0xFFFFFFFFu, 0xFFFFFFFFu, FB_BLUE);
    if (fb_get(160, 100) != FB_GREEN) {
        serial_puts("FB-FAIL clip\n");
        return -1;
    }
    if (fb_get(FB_W, FB_H) != 0) {
        serial_puts("FB-FAIL get-clip\n");
        return -1;
    }
    /* Rect fill: inside set, outside clear. */
    fb_clear(FB_BLACK);
    fb_rect(10, 10, 20, 20, FB_YELLOW);
    if (fb_get(10, 10) != FB_YELLOW ||
        fb_get(29, 29) != FB_YELLOW ||
        fb_get(9, 10) != FB_BLACK || fb_get(10, 9) != FB_BLACK ||
        fb_get(30, 30) != FB_BLACK) {
        serial_puts("FB-FAIL rect\n");
        return -1;
    }
    /* Clipped rect: across the corner, no crash, edge set. */
    fb_rect(FB_W - 5u, FB_H - 5u, 20, 20, FB_CYAN);
    if (fb_get(FB_W - 1u, FB_H - 1u) != FB_CYAN) {
        serial_puts("FB-FAIL rect-clip\n");
        return -1;
    }
    /* Palette round-trip through the DAC. */
    pal_get(FB_RED, &r, &g, &b);
    if (r != 63 || g != 0 || b != 0) {
        serial_puts("FB-FAIL palette\n");
        return -1;
    }
    serial_puts("FB-OK\n");
    return 0;
}
