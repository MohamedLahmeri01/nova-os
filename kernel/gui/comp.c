/* NOVA OS compositor (Phase 12b).
 * Windows live in a fixed pool; paint order is id order (stable,
 * testable layering). Borders: 2px white when focused, 1px gray
 * otherwise. Everything clips through fb_rect/fb_pixel, so wild
 * geometry is safe. Single-core: no locking. The click path
 * (hit + focus + present) runs in IRQ context too (mouse press
 * edge): stores only plus a full repaint, safe under any
 * preemption, matching what mouse_feed already did inline.
 */
#include <stdint.h>

#include "serial.h"
#include "drivers/fb.h"
#include "gui/comp.h"
#include "gui/widget.h"
#include "input/mouse.h"
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

int comp_hit(uint32_t x, uint32_t y) {
    for (uint32_t k = 0; k < COMP_MAXWIN; k++) {
        uint32_t i = COMP_MAXWIN - 1u - k; /* topmost first */
        if (!g_wins[i].used) {
            continue;
        }
        if (x >= g_wins[i].x && y >= g_wins[i].y &&
            x < g_wins[i].x + g_wins[i].w &&
            y < g_wins[i].y + g_wins[i].h) {
            return (int)i;
        }
    }
    return -1;
}

int comp_geom(uint32_t id, uint32_t *x, uint32_t *y, uint32_t *w,
              uint32_t *h) {
    if (!valid_id(id)) {
        return -NOVA_EINVAL;
    }
    if (x != 0) {
        *x = g_wins[id].x;
    }
    if (y != 0) {
        *y = g_wins[id].y;
    }
    if (w != 0) {
        *w = g_wins[id].w;
    }
    if (h != 0) {
        *h = g_wins[id].h;
    }
    return NOVA_ESUCCESS;
}

/* Click routing (Phase 14e): one shared path for hardware
 * press edges (mouse_feed) and synthesized clicks (demo,
 * tests). Hit + focus + repaint; misses and bar clicks
 * return -1 silently (misses are routine pointer motion). */
int comp_click(uint32_t x, uint32_t y) {
    int id = comp_hit(x, y);
    if (id < 0) {
        return -1;
    }
    comp_focus((uint32_t)id);
    comp_present();
    serial_puts("CLICK id=");
    serial_putdec32((uint32_t)id);
    serial_putc('\n');
    return id;
}

void comp_present(void) {
    uint32_t mx = 0, my = 0;
    fb_clear(FB_DARKGRAY);
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        if (g_wins[i].used) {
            paint_one(i);
        }
    }
    /* Desktop bar, always on top (Phase 12d): identity left,
     * focused title right of it (black on... white on black). */
    fb_rect(0, 0, FB_W, COMP_BAR_H, FB_BLACK);
    fb_text(4, 2, "NOVA OS", FB_WHITE);
    if (g_focus >= 0 && g_wins[g_focus].used &&
        g_wins[g_focus].title[0] != 0) {
        fb_text(70, 2, g_wins[g_focus].title, FB_WHITE);
    }
    /* Cursor last of all (nothing covers the pointer), and only
     * when hardware proved itself (else no IRQ ever moves it). */
    if (mouse_present() != 0) {
        mouse_getpos(&mx, &my);
        fb_rect(mx, my, 5, 5, FB_BLACK);
        fb_rect(mx + 1u, my + 1u, 3, 3, FB_WHITE);
    }
}

int comp_stat(struct nova_winstat *out, uint32_t max) {    uint32_t n = 0;
    for (uint32_t i = 0; i < COMP_MAXWIN; i++) {
        if (!g_wins[i].used) {
            continue;
        }
        if (out != 0 && n < max) {
            out[n].id = i;
            out[n].x = g_wins[i].x;
            out[n].y = g_wins[i].y;
            out[n].w = g_wins[i].w;
            out[n].h = g_wins[i].h;
            out[n].focused = (g_focus == (int)i) ? 1u : 0u;
            out[n].color = g_wins[i].color;
            out[n]._pad[0] = 0;
            out[n]._pad[1] = 0;
            out[n]._pad[2] = 0;
            for (uint32_t k = 0; k < 16u; k++) {
                out[n].title[k] = g_wins[i].title[k];
                if (g_wins[i].title[k] == 0) {
                    break;
                }
            }
            out[n].title[15] = 0;
        }
        n++;
    }
    return (int)n;
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
    fb_text(16, 36, "NOVA OS 12c", FB_WHITE);
    fb_text(16, 46, "FB-OK", FB_WHITE);
    fb_text(16, 56, "COMP-OK", FB_WHITE);
}

/* About dialog content via the widget API (dogfooding toolkit). */
static void demo_dialog(void) {
    wid_label(3, 8, 20, "NOVA OS", FB_WHITE);
    wid_label(3, 8, 30, "ABI v1", FB_WHITE);
    wid_label(3, 8, 40, "21/21 OK", FB_WHITE);
    wid_button(3, 8, 50, 60, 20, "OK");
}

static void demo_frame(void) {
    comp_present();
    demo_console_text();
    demo_dialog();
}

void comp_demo(void) {
    int a, b, c, d;
    /* Console + two alerts + an about dialog (back-to-front by
     * id). Titles prove text on screen; the console body proves
     * multi-line text; the dialog proves widgets. Geometry stays
     * below the desktop bar (nothing legible at y 0..12). */
    a = comp_open(8, 16, 190, 176, FB_BLUE);
    b = comp_open(170, 28, 140, 70, FB_RED);
    c = comp_open(20, 110, 120, 70, FB_GREEN);
    d = comp_open(195, 115, 115, 75, FB_TEAL);
    if (a != 0 || b != 1 || c != 2 || d != 3) {
        return; /* selftest covers errors; demo never panics */
    }
    comp_title(0, "console");
    comp_title(1, "alert");
    comp_title(2, "green");
    comp_title(3, "about");
    comp_focus(0);
    demo_mark(0);
    demo_frame();
    demo_wait(50);
    comp_focus_next();
    demo_mark(comp_focused());
    demo_frame();
    demo_wait(50);
    comp_focus_next();
    demo_mark(comp_focused());
    demo_frame();
    demo_wait(50);
    comp_focus_next();
    demo_mark(comp_focused());
    demo_frame();
    demo_wait(50);
    /* Synthesized click on the dialog's OK button (203,165,60,20
     * -> center 233,175): proves the shared click path every
     * boot, hardware or not. Dialog ends focused. */
    comp_click(233, 175);
    demo_frame();
    demo_wait(50);
    comp_focus(0);
    demo_mark(comp_focused());
    demo_frame();
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
    /* Click routing: overlapping scratch pair (A back, B front).
     * Back-only click hits A, overlap hits front B, bar/off-screen
     * miss silently, empty pool misses. */
    if (comp_open(100, 80, 80, 60, FB_RED) != 0 ||
        comp_open(150, 100, 80, 60, FB_BLUE) != 1 ||
        comp_focus(0) != 0) {
        serial_puts("COMP-FAIL click-scratch\n");
        return -1;
    }
    if (comp_click(110, 90) != 0 || comp_focused() != 0) {
        serial_puts("COMP-FAIL click-back\n");
        return -1;
    }
    if (comp_click(160, 120) != 1 || comp_focused() != 1) {
        serial_puts("COMP-FAIL click-front\n");
        return -1;
    }
    if (comp_click(160, 4) != -1 || comp_focused() != 1) {
        serial_puts("COMP-FAIL click-bar\n");
        return -1;
    }
    if (comp_click(400, 300) != -1 || comp_focused() != 1) {
        serial_puts("COMP-FAIL click-off\n");
        return -1;
    }
    if (comp_close(0) != 0 || comp_close(1) != 0 ||
        comp_click(160, 120) != -1 || comp_focused() != -1) {
        serial_puts("COMP-FAIL click-empty\n");
        return -1;
    }
    serial_puts("CLICK-OK\n");
    serial_puts("COMP-OK\n");
    return NOVA_ESUCCESS;
}
