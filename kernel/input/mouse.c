/* NOVA OS PS/2 mouse driver (Phase 12e).
 * 8042 init (aux enable + IRQ12 + reset + reporting) is polled and
 * bounded (boot context, interrupts on: every wait caps iterations
 * and fails instead of hanging). The IRQ path only advances a
 * 3-byte assembler + cursor state (stores only: safe under any
 * preemption, including mid-present). Click handling (hit-test +
 * focus + present) reuses the compositor primitives.
 */
#include <stdint.h>

#include "serial.h"
#include "input/mouse.h"
#include "gui/comp.h"
#include "drivers/fb.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "mouse: no arch backend for this architecture"
#endif

#define PS2_STATUS 0x64u
#define PS2_DATA 0x60u
#define PS2_ST_OUTFULL 0x01u
#define PS2_ST_INFULL 0x02u
/* Short polls: a live controller answers synchronously (next
 * poll after the write), so a miss means deaf, not slow. */
#define PS2_MAXWAIT 5000u

static volatile uint32_t g_mx = 160;
static volatile uint32_t g_my = 100;
static volatile uint32_t g_btn;
static volatile uint32_t g_idx;
static volatile uint8_t g_pkt[3];
/* Live hardware behind the driver (reset proof passed). Without
 * it the parser/click logic still selftests, but no IRQ ever
 * fires and the cursor stays hidden. */
static volatile int g_present;

/* Bounded 8042 polls (0 ok, nonzero on timeout). */
static int wait_in_empty(void) {
    for (uint32_t i = 0; i < PS2_MAXWAIT; i++) {
        if ((arch_inb(PS2_STATUS) & PS2_ST_INFULL) == 0) {
            return 0;
        }
    }
    return -NOVA_ETIMEDOUT;
}

static int wait_out_full(void) {
    for (uint32_t i = 0; i < PS2_MAXWAIT; i++) {
        if ((arch_inb(PS2_STATUS) & PS2_ST_OUTFULL) != 0) {
            return 0;
        }
    }
    return -NOVA_ETIMEDOUT;
}

static int aux_write(uint8_t v) {
    if (wait_in_empty() != 0) {
        return -NOVA_ETIMEDOUT;
    }
    arch_outb(PS2_STATUS, 0xD4u);
    if (wait_in_empty() != 0) {
        return -NOVA_ETIMEDOUT;
    }
    arch_outb(PS2_DATA, v);
    return NOVA_ESUCCESS;
}

static int aux_read(uint8_t *out) {
    if (wait_out_full() != 0) {
        return -NOVA_ETIMEDOUT;
    }
    *out = arch_inb(PS2_DATA);
    return NOVA_ESUCCESS;
}

int mouse_parse_pkt(uint8_t b0, uint8_t b1, uint8_t b2,
                    int32_t *dx, int32_t *dy, uint32_t *btn) {
    int32_t x, y;
    if (dx == 0 || dy == 0 || btn == 0) {
        return -NOVA_EINVAL;
    }
    if ((b0 & 0x08u) == 0) {
        return -NOVA_EINVAL; /* desync: not a first byte */
    }
    if ((b0 & 0xC0u) != 0) {
        return -NOVA_EIO; /* overflow: drop the packet */
    }
    x = (int32_t)b1;
    if ((b0 & 0x10u) != 0) {
        x -= 256;
    }
    y = (int32_t)b2;
    if ((b0 & 0x20u) != 0) {
        y -= 256;
    }
    *dx = x;
    *dy = y;
    *btn = b0 & 0x07u;
    return NOVA_ESUCCESS;
}

static void apply_motion(int32_t dx, int32_t dy) {
    int32_t nx = (int32_t)g_mx + dx;
    int32_t ny = (int32_t)g_my - dy; /* device Y is up */
    if (nx < 0) {
        nx = 0;
    }
    if (ny < 0) {
        ny = 0;
    }
    if (nx >= (int32_t)FB_W) {
        nx = (int32_t)FB_W - 1;
    }
    if (ny >= (int32_t)FB_H) {
        ny = (int32_t)FB_H - 1;
    }
    g_mx = (uint32_t)nx;
    g_my = (uint32_t)ny;
}

void mouse_feed(uint8_t b) {
    if (g_idx == 0 && (b & 0x08u) == 0) {
        return; /* resync: swallow non-first bytes */
    }
    g_pkt[g_idx++] = b;
    if (g_idx < 3u) {
        return;
    }
    g_idx = 0;
    {
        int32_t dx = 0, dy = 0;
        uint32_t btn = 0;
        uint32_t was = g_btn;
        if (mouse_parse_pkt(g_pkt[0], g_pkt[1], g_pkt[2], &dx,
                            &dy, &btn) != 0) {
            return;
        }
        apply_motion(dx, dy);
        g_btn = btn;
        /* Left press edge: focus the topmost window under the
         * cursor (stores only: IRQ-safe). */
        if ((btn & 1u) != 0 && (was & 1u) == 0) {
            int id = comp_hit(g_mx, g_my);
            if (id >= 0) {
                comp_focus((uint32_t)id);
                comp_present();
            }
        }
    }
}

void mouse_irq(void) {
    mouse_feed(arch_inb(PS2_DATA));
}

void mouse_getpos(uint32_t *x, uint32_t *y) {
    if (x != 0) {
        *x = g_mx;
    }
    if (y != 0) {
        *y = g_my;
    }
}

uint32_t mouse_buttons(void) {
    return g_btn;
}

int mouse_present(void) {
    return g_present;
}

int mouse_init(void) {
    uint8_t r;
    /* Liveness probe: aux on, then read the mode byte. A deaf
     * controller (writes black-holed: observed under VBox 7.2
     * here, status reads sane) means no mouse: stay dormant
     * (non-fatal), cursor hidden, IRQ12 never unmasked. */
    if (wait_in_empty() != 0) {
        serial_puts("MOUSE-ABSENT\n");
        return NOVA_ESUCCESS;
    }
    arch_outb(PS2_STATUS, 0xA8u);
    /* Controller command byte: set aux-IRQ (bit 1), keep the rest
     * (notably the keyboard IRQ bit the kbd driver relies on). */
    if (wait_in_empty() != 0) {
        serial_puts("MOUSE-ABSENT\n");
        return NOVA_ESUCCESS;
    }
    arch_outb(PS2_STATUS, 0x20u);
    if (aux_read(&r) != 0) {
        serial_puts("MOUSE-ABSENT\n");
        return NOVA_ESUCCESS;
    }
    if (wait_in_empty() != 0) {
        return -NOVA_ETIMEDOUT;
    }
    arch_outb(PS2_STATUS, 0x60u);
    if (wait_in_empty() != 0) {
        return -NOVA_ETIMEDOUT;
    }
    arch_outb(PS2_DATA, r | 0x02u);
    /* Device reset: ACK + self-test pass + standard ID. */
    if (aux_write(0xFFu) != 0 || aux_read(&r) != 0 || r != 0xFAu ||
        aux_read(&r) != 0 || r != 0xAAu || aux_read(&r) != 0 ||
        r != 0x00u) {
        serial_puts("MOUSE-FAIL reset\n");
        return -NOVA_ENODEV;
    }
    /* Reporting on while IRQ12 is still masked (its ACK is
     * poll-read, so no stray byte can desync the assembler). */
    if (aux_write(0xF4u) != 0 || aux_read(&r) != 0 || r != 0xFAu) {
        serial_puts("MOUSE-FAIL enable\n");
        return -NOVA_ENODEV;
    }
    /* Unmask IRQ12 (slave bit 4); master already open. */
    arch_outb(0xA1u, 0xEFu);
    g_mx = FB_W / 2u;
    g_my = FB_H / 2u;
    g_btn = 0;
    g_idx = 0;
    g_present = 1;
    serial_puts("MOUSE-INIT id=00\n");
    return NOVA_ESUCCESS;
}

int mouse_selftest(void) {
    int32_t dx = 0, dy = 0;
    uint32_t btn = 0;
    /* Decode vectors (signs, zero, buttons, overflow, desync). */
    if (mouse_parse_pkt(0x08u, 5, 7, &dx, &dy, &btn) != 0 ||
        dx != 5 || dy != 7 || btn != 0) {
        serial_puts("MOUSE-FAIL pos\n");
        return -1;
    }
    if (mouse_parse_pkt(0x1Bu, 0xFEu, 0x03u, &dx, &dy, &btn) !=
            0 ||
        dx != -2 || dy != 3 || btn != 3u) {
        serial_puts("MOUSE-FAIL neg\n");
        return -1;
    }
    if (mouse_parse_pkt(0xC8u, 0, 0, &dx, &dy, &btn) !=
        -NOVA_EIO) {
        serial_puts("MOUSE-FAIL overflow\n");
        return -1;
    }
    if (mouse_parse_pkt(0x00u, 0, 0, &dx, &dy, &btn) !=
        -NOVA_EINVAL) {
        serial_puts("MOUSE-FAIL desync\n");
        return -1;
    }
    if (mouse_parse_pkt(0x08u, 0, 0, 0, &dy, &btn) !=
        -NOVA_EINVAL) {
        serial_puts("MOUSE-FAIL null\n");
        return -1;
    }
    /* Assembler resync: garbage first byte swallowed. */
    g_idx = 0;
    mouse_feed(0x00u);
    if (g_idx != 0) {
        serial_puts("MOUSE-FAIL resync\n");
        return -1;
    }
    /* Clamp: run off every edge, stay on screen. */
    mouse_feed(0x08u);
    mouse_feed(200);
    mouse_feed(200);
    if (g_mx != FB_W - 1u || g_my != 0u) {
        serial_puts("MOUSE-FAIL clamp\n");
        return -1;
    }
    /* Click-to-focus on scratch windows (pool is empty: the demo
     * runs later, so ids pack 0,1 deterministically). Front window
     * wins: focusing it must MOVE focus off the back window. */
    if (comp_open(100, 100, 60, 40, FB_RED) != 0 ||
        comp_open(200, 50, 60, 40, FB_BLUE) != 1) {
        serial_puts("MOUSE-FAIL scratch\n");
        return -1;
    }
    /* Step onto the front window: (319,0) -> (210,60). */
    mouse_feed(0x38u);
    mouse_feed(147);
    mouse_feed(196);
    if (g_mx != 210u || g_my != 60u) {
        serial_puts("MOUSE-FAIL step\n");
        return -1;
    }
    if (comp_focused() != 0) {
        serial_puts("MOUSE-FAIL prefocus\n");
        return -1;
    }
    mouse_feed(0x09u); /* left press, no motion */
    mouse_feed(0);
    mouse_feed(0);
    if (g_btn != 1u || comp_focused() != 1) {
        serial_puts("MOUSE-FAIL press\n");
        return -1;
    }
    mouse_feed(0x08u); /* release */
    mouse_feed(0);
    mouse_feed(0);
    if (g_btn != 0u || comp_focused() != 1) {
        serial_puts("MOUSE-FAIL release\n");
        return -1;
    }
    if (comp_close(0) != 0 || comp_close(1) != 0) {
        serial_puts("MOUSE-FAIL unscratch\n");
        return -1;
    }
    serial_puts("MOUSE-OK\n");
    return NOVA_ESUCCESS;
}
