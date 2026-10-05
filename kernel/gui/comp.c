/* NOVA OS compositor (Phase 12b).
 * Windows live in a fixed pool; paint order is id order (stable,
 * testable layering). Borders: 2px white when focused, 1px gray
 * otherwise; title bar is a border-colored strip (no text yet).
 * Everything clips through fb_rect/fb_pixel, so wild geometry is
 * safe. Single-core: no locking (IRQ handlers never compose).
 */
#include <stdint.h>

#include "serial.h"
#include "drivers/fb.h"
#include "gui/comp.h"
#include "syscall/syscall.h"
#include "time/time.h"

struct comp_win {
    int used;
    uint32_t x, y, w, h;
    uint8_t color;
    char title[16];
};

static struct comp_win g_wins[COMP_MAXWIN];
static int g_focus = -1;

static int valid_id(uint32_t id) {
    return id < COMP_MAXWIN && g_wins[id].used;
}

int comp_open(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
              uint8_t color) {
    if (w == 0 || h == 0 || x >= FB_W || y >= FB_H) {
        return -NOVA_EINVAL;
    }
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        if (!g_wins[i].used) {
            g_wins[i].used = 1;
            g_wins[i].x = x;
            g_wins[i].y = y;
            g_wins[i].w = w;
            g_wins[i].h = h;
            g_wins[i].color = color;
            g_wins[i].title[0] = 0;
            if (g_focus < 0) {
                g_focus = (int)i;
            }
            return (int)i;
        }
    }
    return -NOVA_ENOSPC;
}

int comp_close(uint32_t id) {
    if (!valid_id(id)) {
        return -NOVA_EINVAL;
    }
    g_wins[id].used = 0;
    if (g_focus == (int)id) {
        g_focus = -1;
        for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
            if (g_wins[i].used) {
                g_focus = (int)i;
                break;
            }
        }
    }
    return NOVA_ESUCCESS;
}

int comp_focus(uint32_t id) {
    if (!valid_id(id)) {
        return -NOVA_EINVAL;
    }
    g_focus = (int)id;
    return NOVA_ESUCCESS;
}

int comp_title(uint32_t id, const char *s) {
    uint32_t i = 0;
    if (!valid_id(id) || s == 0) {
        return -NOVA_EINVAL;
    }
    while (i + 1u < sizeof(g_wins[id].title) && s[i] != 0) {
        g_wins[id].title[i] = s[i];
        i++;
    }
    g_wins[id].title[i] = 0;
    return NOVA_ESUCCESS;
}

int comp_focus_next(void) {
    if (g_focus < 0) {
        for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
            if (g_wins[i].used) {
                g_focus = (int)i;
                return g_focus;
            }
        }
        return -1;
    }
    for (uint32_t k = 1; k <= COMP_MAXWIN; k++) {
        uint32_t i = ((uint32_t)g_focus + k) % COMP_MAXWIN;
        if (g_wins[i].used) {
            g_focus = (int)i;
            return g_focus;
        }
    }
    return -1;
}

int comp_focused(void) {
    return g_focus;
}

static void paint_one(uint32_t id) {
    uint32_t x = g_wins[id].x;
    uint32_t y = g_wins[id].y;
    uint32_t w = g_wins[id].w;
    uint32_t h = g_wins[id].h;
    uint8_t bc = ((int)id == g_focus) ? FB_WHITE : FB_GRAY;
    uint32_t bw = ((int)id == g_focus) ? 2u : 1u;
    fb_rect(x, y, w, h, g_wins[id].color);
    if (h > COMP_TITLE_H) {
        fb_rect(x, y, w, COMP_TITLE_H, bc);
        if (g_wins[id].title[0] != 0) {
            fb_text(x + 4u, y + 2u, g_wins[id].title, FB_BLACK);
        }
    }
    /* Border outlines (clipped by fb_rect). */
    fb_rect(x, y, w, bw, bc);
    fb_rect(x, y + h - bw, w, bw, bc);
    fb_rect(x, y, bw, h, bc);
    fb_rect(x + w - bw, y, bw, h, bc);
}

void comp_present(void) {
    fb_clear(FB_DARKGRAY);
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        if (g_wins[i].used) {
            paint_one(i);
        }
    }
}

static void demo_wait(uint32_t ticks) {
    uint32_t t0 = g_ticks;
    while (g_ticks - t0 < ticks) {
    }
}

static void demo_mark(int id) {
    serial_puts("COMP-FOCUS id=");
    serial_putdec32((uint32_t)id);
    serial_putc('\n');
}

/* Console body (redrawn after every present: presents repaint). */
static void demo_console_text(void) {
    fb_text(16, 28, "NOVA OS 12c", FB_WHITE);
    fb_text(16, 38, "FB-OK", FB_WHITE);
    fb_text(16, 48, "COMP-OK", FB_WHITE);
}

void comp_demo(void) {
    int a, b, c;
    /* Console + two alerts (back-to-front by id). Titles prove
     * text on screen; the console body proves multi-line text. */
    a = comp_open(8, 8, 190, 184, FB_BLUE);
    b = comp_open(170, 20, 140, 70, FB_RED);
    c = comp_open(150, 110, 150, 80, FB_GREEN);
    if (a != 0 || b != 1 || c != 2) {
        return; /* selftest covers errors; demo never panics */
    }
    comp_title(0, "console");
    comp_title(1, "alert");
    comp_title(2, "green");
    comp_focus(0);
    demo_mark(0);
    comp_present();
    demo_console_text();
    demo_wait(50);
    comp_focus_next();
    demo_mark(comp_focused());
    comp_present();
    demo_console_text();
    demo_wait(50);
    comp_focus_next();
    demo_mark(comp_focused());
    comp_present();
    demo_console_text();
    demo_wait(50);
    comp_focus(0);
    demo_mark(comp_focused());
    comp_present();
    demo_console_text();
}

int comp_selftest(void) {
    int id[COMP_MAXWIN];
    /* Fill the pool (ids pack lowest-first). */
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        id[i] = comp_open(10u * i, 10u * i, 40, 30,
                          (uint8_t)(FB_RED + (i & 7u)));
        if (id[i] != (int)i) {
            serial_puts("COMP-FAIL pack\n");
            return -1;
        }
    }
    /* Full pool refuses. */
    if (comp_open(0, 0, 10, 10, FB_WHITE) != -NOVA_ENOSPC) {
        serial_puts("COMP-FAIL full\n");
        return -1;
    }
    /* Bad geometry refuses (empty, off-screen). */
    if (comp_open(0, 0, 0, 10, FB_WHITE) != -NOVA_EINVAL ||
        comp_open(0, 0, 10, 0, FB_WHITE) != -NOVA_EINVAL ||
        comp_open(FB_W, 0, 10, 10, FB_WHITE) != -NOVA_EINVAL ||
        comp_open(0, FB_H, 10, 10, FB_WHITE) != -NOVA_EINVAL) {
        serial_puts("COMP-FAIL geom\n");
        return -1;
    }
    /* Bad ids refuse everywhere. */
    if (comp_close(COMP_MAXWIN) != -NOVA_EINVAL ||
        comp_focus(COMP_MAXWIN) != -NOVA_EINVAL) {
        serial_puts("COMP-FAIL badid\n");
        return -1;
    }
    /* Focus cycle wraps over the live set. */
    if (comp_focused() != 0) {
        serial_puts("COMP-FAIL focus0\n");
        return -1;
    }
    if (comp_focus(5) != 0 || comp_focused() != 5) {
        serial_puts("COMP-FAIL focus\n");
        return -1;
    }
    if (comp_focus_next() != 6 || comp_focus_next() != 7 ||
        comp_focus_next() != 0) {
        serial_puts("COMP-FAIL cycle\n");
        return -1;
    }
    /* Titles: bad ids and NULL refuse; "X" inks its bar cell
     * (any black pixel in the title zone proves text on screen
     * under BIOS glyphs, fallback boxes, and the host synthetic
     * font alike). */
    if (comp_title(COMP_MAXWIN, "A") != -NOVA_EINVAL ||
        comp_title(7, 0) != -NOVA_EINVAL) {
        serial_puts("COMP-FAIL title-bad\n");
        return -1;
    }
    if (comp_title(7, "X") != 0) {
        serial_puts("COMP-FAIL title\n");
        return -1;
    }
    comp_present();
    {
        uint32_t ink = 0;
        for (uint32_t yy = 72; yy < 80u; yy++) {
            for (uint32_t xx = 74; xx < 82u; xx++) {
                if (fb_get(xx, yy) == FB_BLACK) {
                    ink++;
                }
            }
        }
        if (ink == 0) {
            serial_puts("COMP-FAIL title-ink\n");
            return -1;
        }
    }
    /* Layering: top id wins the overlap point (exact color
     * proves z order: (15,30) sits in window 1's fill, below its
     * title bar and past window 0's edge). */
    comp_present();
    if (fb_get(15, 30) != FB_GREEN) {
        serial_puts("COMP-FAIL paint\n");
        return -1;
    }
    /* Close drops focus to the lowest live id; slot reuses. */
    if (comp_close(0) != 0 || comp_focused() != 1) {
        serial_puts("COMP-FAIL close-focus\n");
        return -1;
    }
    if (comp_open(200, 150, 40, 30, FB_WHITE) != 0) {
        serial_puts("COMP-FAIL reuse\n");
        return -1;
    }
    /* Drain the pool (clean screen for the demo). */
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        if (g_wins[i].used && comp_close(i) != 0) {
            serial_puts("COMP-FAIL drain\n");
            return -1;
        }
    }
    if (comp_focus_next() != -1 || comp_focused() != -1) {
        serial_puts("COMP-FAIL empty\n");
        return -1;
    }
    serial_puts("COMP-OK\n");
    return NOVA_ESUCCESS;
}
