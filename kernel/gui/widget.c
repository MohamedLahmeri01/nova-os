/* NOVA OS widgets: labels + buttons (Phase 14d, 14e-ii press
 * states). Window-relative drawing over the compositor
 * (geometry fetched per call, so moves resize correctly).
 * Everything funnels into clipped fb primitives: wild
 * coordinates are safe. Stateless: hit queries + pressed-look
 * rendering only, no press tracking (no registry).
 */
#include <stdint.h>

#include "serial.h"
#include "drivers/fb.h"
#include "gui/comp.h"
#include "gui/widget.h"
#include "syscall/syscall.h"

int wid_label(uint32_t id, uint32_t dx, uint32_t dy, const char *s,
              uint8_t color) {
    uint32_t x, y;
    if (s == 0) {
        return -NOVA_EINVAL;
    }
    if (comp_geom(id, &x, &y, 0, 0) != 0) {
        return -NOVA_EINVAL;
    }
    fb_text(x + dx, y + dy, s, color);
    return NOVA_ESUCCESS;
}

/* Shared button painter (pressed inverts the bevel and nudges
 * the label +1,+1, like a key traveling down). */
static int button_draw(uint32_t id, uint32_t dx, uint32_t dy,
                       uint32_t w, uint32_t h, const char *label,
                       int pressed) {
    uint32_t x, y;
    uint32_t n = 0;
    uint32_t tx, ty;
    uint8_t hi, lo;
    if (label == 0 || w < 16u || h < 14u) {
        return -NOVA_EINVAL;
    }
    if (comp_geom(id, &x, &y, 0, 0) != 0) {
        return -NOVA_EINVAL;
    }
    x += dx;
    y += dy;
    /* Face + bevel (up: white top/left, dark bottom/right;
     * down: inverted). */
    hi = pressed ? FB_DARKGRAY : FB_WHITE;
    lo = pressed ? FB_WHITE : FB_DARKGRAY;
    fb_rect(x, y, w, h, FB_GRAY);
    fb_rect(x, y, w, 1, hi);
    fb_rect(x, y, 1, h, hi);
    fb_rect(x, y + h - 1u, w, 1, lo);
    fb_rect(x + w - 1u, y, 1, h, lo);
    /* Centered label (clipped when wider than the face). */
    while (n < 64u && label[n] != 0) {
        n++;
    }
    tx = (n * 8u >= w) ? x + 2u : x + (w - n * 8u) / 2u;
    ty = y + (h - 8u) / 2u;
    if (pressed) {
        tx += 1u;
        ty += 1u;
    }
    for (uint32_t i = 0; i < n && i < 64u; i++) {
        /* Draw char by char (fb_text would restart at x). */
        char one[2] = { label[i], 0 };
        fb_text(tx + i * 8u, ty, one, FB_BLACK);
    }
    return NOVA_ESUCCESS;
}

int wid_button(uint32_t id, uint32_t dx, uint32_t dy, uint32_t w,
               uint32_t h, const char *label) {
    return button_draw(id, dx, dy, w, h, label, 0);
}

int wid_button_down(uint32_t id, uint32_t dx, uint32_t dy,
                    uint32_t w, uint32_t h, const char *label) {
    return button_draw(id, dx, dy, w, h, label, 1);
}

int wid_hit(uint32_t id, uint32_t dx, uint32_t dy, uint32_t w,
            uint32_t h, uint32_t px, uint32_t py) {
    uint32_t x, y;
    if (comp_geom(id, &x, &y, 0, 0) != 0) {
        return -NOVA_EINVAL;
    }
    x += dx;
    y += dy;
    /* Left/top in, right/bottom out (comp_hit edges). Wraps
     * safely: additions stay far below 2^32 on 320x200. */
    if (px >= x && py >= y && px < x + w && py < y + h) {
        return 1;
    }
    return 0;
}

int wid_selftest(void) {
    int id;
    /* Bad ids and NULLs refuse (no drawing attempted). */
    if (wid_label(COMP_MAXWIN, 0, 0, "X", FB_WHITE) !=
            -NOVA_EINVAL ||
        wid_label(0, 0, 0, 0, FB_WHITE) != -NOVA_EINVAL ||
        wid_button(COMP_MAXWIN, 0, 0, 40, 20, "OK") !=
            -NOVA_EINVAL ||
        wid_button(0, 0, 0, 8, 8, "OK") != -NOVA_EINVAL) {
        serial_puts("WID-FAIL bad\n");
        return -1;
    }
    /* Scratch window (pool empty: runs after comp_selftest
     * drains, before the demo paints). */
    id = comp_open(50, 150, 120, 60, FB_BLUE);
    if (id != 0) {
        serial_puts("WID-FAIL open\n");
        return -1;
    }
    /* Label inks its cell ("X" under every font). */
    if (wid_label(0, 8, 20, "X", FB_WHITE) != 0) {
        serial_puts("WID-FAIL label\n");
        return -1;
    }
    {
        uint32_t ink = 0;
        for (uint32_t yy = 170; yy < 178u; yy++) {
            for (uint32_t xx = 58; xx < 66u; xx++) {
                if (fb_get(xx, yy) == FB_WHITE) {
                    ink++;
                }
            }
        }
        if (ink == 0) {
            serial_puts("WID-FAIL ink\n");
            return -1;
        }
    }
    /* Button bevel is exact (font-independent geometry). */
    if (wid_button(0, 8, 30, 60, 20, "OK") != 0) {
        serial_puts("WID-FAIL button\n");
        return -1;
    }
    /* Button at (58,180): white top/left, dark bottom/right,
     * gray face, black label ink somewhere inside. */
    if (fb_get(58, 180) != FB_WHITE || fb_get(58, 181) != FB_WHITE ||
        fb_get(58, 199) != FB_DARKGRAY ||
        fb_get(117, 180) != FB_DARKGRAY ||
        fb_get(60, 185) != FB_GRAY) {
        serial_puts("WID-FAIL bevel\n");
        return -1;
    }
    {
        uint32_t ink = 0;
        for (uint32_t yy = 182; yy < 197u; yy++) {
            for (uint32_t xx = 60; xx < 115u; xx++) {
                if (fb_get(xx, yy) == FB_BLACK) {
                    ink++;
                }
            }
        }
        if (ink == 0) {
            serial_puts("WID-FAIL text\n");
            return -1;
        }
    }
    /* Hit query: bad window refuses; rect edges match comp_hit
     * (button at 58,180 spans 60x20). */
    if (wid_button_down(COMP_MAXWIN, 0, 0, 40, 20, "OK") !=
            -NOVA_EINVAL ||
        wid_button_down(0, 0, 0, 8, 8, "OK") != -NOVA_EINVAL ||
        wid_button_down(0, 0, 0, 40, 20, 0) != -NOVA_EINVAL ||
        wid_hit(COMP_MAXWIN, 0, 0, 40, 20, 58, 180) !=
            -NOVA_EINVAL) {
        serial_puts("WID-FAIL press-bad\n");
        return -1;
    }
    if (wid_hit(0, 8, 30, 60, 20, 58, 180) != 1 ||
        wid_hit(0, 8, 30, 60, 20, 117, 199) != 1 ||
        wid_hit(0, 8, 30, 60, 20, 118, 180) != 0 ||
        wid_hit(0, 8, 30, 60, 20, 58, 200) != 0 ||
        wid_hit(0, 8, 30, 60, 20, 0, 0) != 0) {
        serial_puts("WID-FAIL hit\n");
        return -1;
    }
    /* Pressed look: inverted bevel, gray face kept, label ink
     * nudged but inside the same scan box. */
    if (wid_button_down(0, 8, 30, 60, 20, "OK") != 0) {
        serial_puts("WID-FAIL down\n");
        return -1;
    }
    if (fb_get(58, 180) != FB_DARKGRAY ||
        fb_get(58, 181) != FB_DARKGRAY ||
        fb_get(58, 199) != FB_WHITE ||
        fb_get(117, 180) != FB_WHITE ||
        fb_get(60, 185) != FB_GRAY) {
        serial_puts("WID-FAIL down-bevel\n");
        return -1;
    }
    {
        uint32_t ink = 0;
        for (uint32_t yy = 182; yy < 197u; yy++) {
            for (uint32_t xx = 60; xx < 115u; xx++) {
                if (fb_get(xx, yy) == FB_BLACK) {
                    ink++;
                }
            }
        }
        if (ink == 0) {
            serial_puts("WID-FAIL down-text\n");
            return -1;
        }
    }
    serial_puts("PRESS-OK\n");
    if (comp_close(0) != 0) {
        serial_puts("WID-FAIL close\n");
        return -1;
    }
    serial_puts("WID-OK\n");
    return NOVA_ESUCCESS;
}
