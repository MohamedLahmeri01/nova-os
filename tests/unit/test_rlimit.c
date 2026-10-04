/* NOVA OS rlimit host test (Phase 10b, T16).
 * Host-side: the REAL kernel/process/rlimit.c, pure logic only.
 * Set-rule matrix (lower/raise/over-hard/NULL), fsize clamp
 * vectors, randomized fuzz for crash-freedom and bound obedience.
 * VERDICT line.
 */
#include <stdio.h>
#include <stdint.h>

#include "process/rlimit.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

static uint32_t lcg_state = 0x8117u;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(void) {
    uint32_t out = 0xFFFFFFFFu;
    uint32_t ok = 0xFFFFFFFFu;

    /* Set-rule matrix. */
    check(rlimit_set_rule(16, 2, 16, &out) == 0 && out == 2,
          "set-lower", out, 0);
    check(rlimit_set_rule(16, 16, 16, &out) == 0 && out == 16,
          "set-same", out, 0);
    check(rlimit_set_rule(16, 0, 16, &out) == 0 && out == 0,
          "set-zero", out, 0);
    check(rlimit_set_rule(2, 3, 16, &out) == -NOVA_EPERM,
          "set-raise", 0, 0);
    check(rlimit_set_rule(16, 17, 16, &out) == -NOVA_EINVAL,
          "set-hard", 0, 0);
    check(rlimit_set_rule(16, 0xFFFFFFFFu, 16, &out) ==
              -NOVA_EINVAL,
          "set-huge", 0, 0);
    check(rlimit_set_rule(16, 2, 16, 0) == -NOVA_EINVAL,
          "set-null", 0, 0);

    /* Fsize clamp vectors. */
    check(rlimit_fsize_allow(100, 0, 50, &ok) == 0 && ok == 50,
          "clamp-open", ok, 0);
    check(rlimit_fsize_allow(100, 80, 50, &ok) == 0 && ok == 20,
          "clamp-trim", ok, 0);
    check(rlimit_fsize_allow(100, 99, 50, &ok) == 0 && ok == 1,
          "clamp-edge", ok, 0);
    check(rlimit_fsize_allow(100, 100, 10, &ok) == -NOVA_EFBIG,
          "clamp-full", 0, 0);
    check(rlimit_fsize_allow(100, 150, 10, &ok) == -NOVA_EFBIG,
          "clamp-past", 0, 0);
    check(rlimit_fsize_allow(100, 0, 0, &ok) == 0 && ok == 0,
          "clamp-empty", ok, 0);
    check(rlimit_fsize_allow(100, 0, 50, 0) == -NOVA_EINVAL,
          "clamp-null", 0, 0);

    /* Fuzz: random (lim, off, len) obeys the contract. */
    for (int t = 0; t < 2000; t++) {
        uint32_t lim = lcg() % 70000u;
        uint32_t off = lcg() % 70000u;
        uint32_t len = lcg() % 70000u;
        uint32_t o = 0xFFFFFFFFu;
        int r = rlimit_fsize_allow(lim, off, len, &o);
        if (off >= lim) {
            check(r == -NOVA_EFBIG, "fuzz-full", off, lim);
        } else {
            uint32_t want = (len > lim - off) ? lim - off : len;
            check(r == 0 && o == want, "fuzz-ok", o, want);
            check(off + o <= lim, "fuzz-bound", off + o, lim);
            check(o <= len, "fuzz-nogrow", o, len);
        }
    }
    printf("fuzz: 2000 fsize clamps vs contract\n");

    /* Fuzz: set-rule never widens, never crashes. */
    for (int t = 0; t < 2000; t++) {
        uint32_t cur = lcg() % 20u;
        uint32_t val = lcg() % 20u;
        uint32_t o = 0xFFFFFFFFu;
        int r = rlimit_set_rule(cur, val, 16, &o);
        if (val > 16) {
            check(r == -NOVA_EINVAL, "fuzz2-hard", val, 0);
        } else if (val > cur) {
            check(r == -NOVA_EPERM, "fuzz2-raise", val, cur);
        } else {
            check(r == 0 && o == val, "fuzz2-ok", o, val);
        }
    }
    printf("fuzz: 2000 set-rules vs monotonicity\n");

    if (g_failed) {
        printf("VERDICT: rlimit BROKEN\n");
        return 1;
    }
    printf("VERDICT: rlimit correct\n");
    return 0;
}
