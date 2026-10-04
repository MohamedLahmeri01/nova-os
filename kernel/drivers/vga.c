/* NOVA OS VGA text console (Phase 8d).
 * 80x25x2 framebuffer at 0xB8000. Position tracked in software
 * (row/col); the hardware cursor follows every putc. Scroll by
 * copying rows up (3840B, bounded, no heap). cli/sti not needed:
 * single-core syscalls + IRQ handlers only append (all paths funnel
 * through the same putc; a torn cursor update self-heals next char).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "drivers/vga.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "vga: no arch backend for this architecture"
#endif

#define VGA_BASE ((volatile uint16_t *)0xB8000u)
#define VGA_COLS 80u
#define VGA_ROWS 25u
#define VGA_ATTR 0x07u
#define CRT_IDX 0x3D4u
#define CRT_DATA 0x3D5u

static uint32_t g_row;
static uint32_t g_col;

static void cursor_sync(void) {
    uint16_t pos = (uint16_t)(g_row * VGA_COLS + g_col);
    arch_outb(CRT_IDX, 14);
    arch_outb(CRT_DATA, (uint8_t)(pos >> 8));
    arch_outb(CRT_IDX, 15);
    arch_outb(CRT_DATA, (uint8_t)(pos & 0xFFu));
}

static uint16_t cursor_read(void) {
    uint16_t hi, lo;
    arch_outb(CRT_IDX, 14);
    hi = arch_inb(CRT_DATA);
    arch_outb(CRT_IDX, 15);
    lo = arch_inb(CRT_DATA);
    return (uint16_t)((hi << 8) | lo);
}

static void scroll(void) {
    for (uint32_t r = 0; r + 1u < VGA_ROWS; r++) {
        for (uint32_t c = 0; c < VGA_COLS; c++) {
            VGA_BASE[r * VGA_COLS + c] =
                VGA_BASE[(r + 1u) * VGA_COLS + c];
        }
    }
    for (uint32_t c = 0; c < VGA_COLS; c++) {
        VGA_BASE[(VGA_ROWS - 1u) * VGA_COLS + c] =
            (uint16_t)((VGA_ATTR << 8) | ' ');
    }
}

void vga_init(void) {
    for (uint32_t i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        VGA_BASE[i] = (uint16_t)((VGA_ATTR << 8) | ' ');
    }
    g_row = 0;
    g_col = 0;
    cursor_sync();
}

void vga_putc(char c) {
    if (c == '\r') {
        g_col = 0;
    } else if (c == '\n') {
        g_col = 0;
        g_row++;
    } else if (c == '\b') {
        if (g_col > 0) {
            g_col--;
        }
    } else if (c == '\t') {
        do {
            vga_putc(' ');
        } while ((g_col & 7u) != 0 && g_col < VGA_COLS);
        cursor_sync();
        return;
    } else {
        VGA_BASE[g_row * VGA_COLS + g_col] =
            (uint16_t)((VGA_ATTR << 8) | (uint8_t)c);
        g_col++;
        if (g_col >= VGA_COLS) {
            g_col = 0;
            g_row++;
        }
    }
    while (g_row >= VGA_ROWS) {
        scroll();
        g_row = VGA_ROWS - 1u;
    }
    cursor_sync();
}

int vga_selftest(void) {
    volatile uint16_t *cell =
        &VGA_BASE[(VGA_ROWS - 1u) * VGA_COLS + (VGA_COLS - 1u)];
    uint16_t saved = *cell;
    uint32_t prow = g_row;
    uint32_t pcol = g_col;
    /* Cell write/readback at the corner (restored after). */
    *cell = (uint16_t)((VGA_ATTR << 8) | 'Z');
    if (*cell != (uint16_t)((VGA_ATTR << 8) | 'Z')) {
        serial_puts("VGA-FAIL cell\n");
        return -1;
    }
    *cell = saved;
    /* Cursor round-trip (set corner, read back, restore). */
    g_row = VGA_ROWS - 1u;
    g_col = VGA_COLS - 1u;
    cursor_sync();
    if (cursor_read() != VGA_ROWS * VGA_COLS - 1u) {
        serial_puts("VGA-FAIL cursor\n");
        return -1;
    }
    g_row = prow;
    g_col = pcol;
    cursor_sync();
    serial_puts("VGA-OK\n");
    return NOVA_ESUCCESS;
}
