/* NOVA OS mouse host test (Phase 12e, T20).
 * Host-side: the REAL kernel/input/mouse.c against stub serial +
 * stub compositor (hit programmable, focus/present recorded).
 * Parser matrix (signs, buttons, overflow, desync, NULL),
 * assembler resync/clamp, click-to-focus wiring. The 8042 init
 * never runs on host (needs the controller); the guest proves
 * reset + ID (MOUSE-INIT). VERDICT line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "input/mouse.h"
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

/* Compositor stubs with a call record (click wiring proof). */
static int stub_hit = -1;
static int focus_calls;
static int focus_last = -2;
static int present_calls;
static int click_calls;
static int click_last = -2;

int comp_hit(uint32_t x, uint32_t y) {
    (void)x;
    (void)y;
    return stub_hit;
}

int comp_focus(uint32_t id) {
    focus_calls++;
    focus_last = (int)id;
    return 0;
}

void comp_present(void) {
    present_calls++;
}

/* Shared click path stub: mirrors the real semantics (hit +
 * focus + present, silent miss) so the press edge proves
 * routing through comp_click, not past it. */
int comp_click(uint32_t x, uint32_t y) {
    int id = comp_hit(x, y);
    click_calls++;
    click_last = id;
    if (id < 0) {
        return -1;
    }
    comp_focus((uint32_t)id);
    comp_present();
    return id;
}

/* Referenced only by mouse_selftest (never runs on host). */
int comp_open(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
              uint8_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    return 0;
}

int comp_close(uint32_t id) {
    (void)id;
    return 0;
}

int comp_focused(void) {
    return -1;
}

int main(void) {
    int32_t dx = 0, dy = 0;
    uint32_t btn = 0;
    int rc = 0;

    /* Decode matrix (call and assert separately: combined
     * call-in-check expressions let the optimizer hoist the
     * print loads above the call, printing stale values). */
    rc = mouse_parse_pkt(0x08u, 5, 7, &dx, &dy, &btn);
    check(rc == 0 && dx == 5 && dy == 7 && btn == 0, "pos",
          (unsigned long)dx, (unsigned long)dy);
    rc = mouse_parse_pkt(0x1Bu, 0xFEu, 0x03u, &dx, &dy, &btn);
    check(rc == 0 && dx == -2 && dy == 3 && btn == 3u, "neg",
          (unsigned long)dx, btn);
    rc = mouse_parse_pkt(0x38u, 0xFFu, 0xFFu, &dx, &dy, &btn);
    check(rc == 0 && dx == -1 && dy == -1 && btn == 0, "neg1",
          (unsigned long)dx, (unsigned long)dy);
    rc = mouse_parse_pkt(0xC8u, 0, 0, &dx, &dy, &btn);
    check(rc == -NOVA_EIO, "overflow", 0, 0);
    rc = mouse_parse_pkt(0x00u, 0, 0, &dx, &dy, &btn);
    check(rc == -NOVA_EINVAL, "desync", 0, 0);
    rc = mouse_parse_pkt(0x08u, 0, 0, 0, &dy, &btn);
    check(rc == -NOVA_EINVAL, "null", 0, 0);

    /* Assembler: garbage swallowed, motion clamps on screen. */
    {
        uint32_t x = 0, y = 0;
        mouse_feed(0x00u);
        mouse_feed(0x08u);
        mouse_feed(200);
        mouse_feed(200);
        mouse_getpos(&x, &y);
        check(x == 319u && y == 0u, "clamp", x, y);
    }

    /* Click wiring: press edge routes through comp_click to
     * the hit window (focus + repaint); release changes
     * nothing further. */
    stub_hit = 4;
    focus_calls = 0;
    present_calls = 0;
    click_calls = 0;
    mouse_feed(0x09u);
    mouse_feed(0);
    mouse_feed(0);
    check(mouse_buttons() == 1u, "press-btn", mouse_buttons(),
          0);
    check(click_calls == 1 && click_last == 4, "press-click",
          (unsigned long)click_calls, (unsigned long)click_last);
    check(focus_calls == 1 && focus_last == 4, "press-focus",
          (unsigned long)focus_calls, (unsigned long)focus_last);
    check(present_calls == 1, "press-present",
          (unsigned long)present_calls, 0);
    mouse_feed(0x08u);
    mouse_feed(0);
    mouse_feed(0);
    check(mouse_buttons() == 0u, "release-btn",
          mouse_buttons(), 0);
    check(focus_calls == 1, "release-quiet",
          (unsigned long)focus_calls, 0);
    check(click_calls == 1, "release-noclick",
          (unsigned long)click_calls, 0);

    /* Miss: no hit, no focus call, no repaint, button still
     * tracks. */
    stub_hit = -1;
    mouse_feed(0x09u);
    mouse_feed(0);
    mouse_feed(0);
    check(mouse_buttons() == 1u, "miss-btn", mouse_buttons(),
          0);
    check(click_calls == 2 && click_last == -1, "miss-click",
          (unsigned long)click_calls, 0);
    check(focus_calls == 1, "miss-nofocus",
          (unsigned long)focus_calls, 0);
    check(present_calls == 1, "miss-nopresent",
          (unsigned long)present_calls, 0);
    mouse_feed(0x08u);
    mouse_feed(0);
    mouse_feed(0);

    if (g_failed) {
        printf("VERDICT: mouse BROKEN\n");
        return 1;
    }
    printf("VERDICT: mouse correct\n");
    return 0;
}
