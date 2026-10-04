/* NOVA OS capability host test (Phase 10a, T15).
 * Host-side: the REAL kernel/process/caps.c, pure logic only (no
 * scheduler/process required). Allow/drop matrix, unknown-bit
 * discipline, monotonicity over all mask pairs, randomized fuzz
 * for crash-freedom. VERDICT line.
 */
#include <stdio.h>
#include <stdint.h>

#include "process/caps.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

static uint32_t lcg_state = 0xC0FFEEu;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(void) {
    uint32_t out = 0xFFFFFFFFu;

    /* Allow matrix: held rights pass, missing rights deny,
     * unknown rights are caller bugs (EINVAL, not EPERM). */
    check(caps_allow(CAPS_ALL, CAP_NET) == 0, "allow-net", 0, 0);
    check(caps_allow(CAPS_ALL, CAP_SPAWN) == 0, "allow-spawn", 0,
          0);
    check(caps_allow(CAPS_ALL, CAP_NET | CAP_SPAWN) == 0,
          "allow-both", 0, 0);
    check(caps_allow(0, CAP_NET) == -NOVA_EPERM, "deny-net", 0,
          0);
    check(caps_allow(CAP_NET, CAP_SPAWN) == -NOVA_EPERM,
          "deny-spawn", 0, 0);
    check(caps_allow(CAP_NET, CAP_NET | CAP_SPAWN) ==
              -NOVA_EPERM,
          "deny-partial", 0, 0);
    check(caps_allow(CAPS_ALL, 0x04u) == -NOVA_EINVAL,
          "allow-unknown", 0, 0);
    check(caps_allow(CAPS_ALL, 0) == 0, "allow-empty", 0, 0);

    /* Drop matrix: monotonic shrink, unknown bits refuse,
     * NULL out refuses. */
    check(caps_drop(CAPS_ALL, CAP_NET, &out) == 0 &&
              out == CAP_SPAWN,
          "drop-net", out, 0);
    check(caps_drop(CAPS_ALL, CAPS_ALL, &out) == 0 && out == 0,
          "drop-all", out, 0);
    check(caps_drop(CAPS_ALL, 0, &out) == 0 && out == CAPS_ALL,
          "drop-none", out, 0);
    check(caps_drop(0, CAP_NET, &out) == 0 && out == 0,
          "drop-empty", out, 0);
    check(caps_drop(CAPS_ALL, 0x04u, &out) == -NOVA_EINVAL,
          "drop-unknown", 0, 0);
    check(caps_drop(CAPS_ALL, 0xFFFFFFFFu, &out) ==
              -NOVA_EINVAL,
          "drop-huge", 0, 0);
    check(caps_drop(CAPS_ALL, CAP_NET, 0) == -NOVA_EINVAL,
          "drop-null", 0, 0);

    /* Monotonicity over every (caps, mask) pair of known bits:
     * drops never add rights. */
    for (uint32_t c = 0; c < 4u; c++) {
        for (uint32_t m = 0; m < 4u; m++) {
            uint32_t o = 0xFFFFFFFFu;
            check(caps_drop(c, m, &o) == 0 && o == (c & ~m),
                  "mono", c, m);
            check((o & ~c) == 0, "mono-subset", o, c);
        }
    }

    /* Fuzz: random masks/caps never crash, never widen. */
    for (int t = 0; t < 2000; t++) {
        uint32_t c = lcg();
        uint32_t m = lcg();
        uint32_t o = 0xFFFFFFFFu;
        int r = caps_drop(c, m, &o);
        if ((m & ~CAPS_ALL) != 0) {
            check(r == -NOVA_EINVAL, "fuzz-badbit", m, 0);
        } else {
            check(r == 0 && o == (c & ~m), "fuzz-ok", o, c);
        }
        check(caps_allow(c, CAPS_ALL) == 0 ||
                  caps_allow(c, CAPS_ALL) == -NOVA_EPERM,
              "fuzz-allow-rc", 0, 0);
    }
    printf("fuzz: 2000 cap masks vs monotonicity\n");

    if (g_failed) {
        printf("VERDICT: caps BROKEN\n");
        return 1;
    }
    printf("VERDICT: caps correct\n");
    return 0;
}
