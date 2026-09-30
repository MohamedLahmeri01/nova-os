/* NOVA OS PMM shootout harness (host, Phase 2a).
 * Drives ONE linked backend through identical deterministic workloads
 * with a reference live-set model. Any invariant violation fails loudly.
 * Timing: C11 timespec_get (ms). PRNG: fixed seed (deterministic).
 *
 * Workloads mirror the 1GB test box: base 0x100000, 262128 frames, with
 * the same top-carve metadata policy as kernel/mm/pmm.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "pmm.h"

#define ARENA_BASE 0x100000u
/* Exact VBox 1GB-box usable region above 1MB: len 0x3FEF0000. */
#define ARENA_FRAMES 261872u

static uint8_t *g_live;
static uint32_t g_managed;
static int g_failed;

static double now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void check(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        g_failed = 1;
    }
}

static uint32_t idx_of(uint32_t addr) {
    return (addr - ARENA_BASE) / NOVA_FRAME_SIZE;
}

static void setup_arena(void) {
    uint32_t need = be_meta_needed(ARENA_FRAMES);
    uint32_t meta_frames = (need + NOVA_FRAME_SIZE - 1u) / NOVA_FRAME_SIZE;
    g_managed = ARENA_FRAMES - meta_frames;
    void *meta = malloc(need);
    if (!meta) {
        printf("FAIL: host malloc for metadata\n");
        exit(2);
    }
    g_live = (uint8_t *)calloc(g_managed, 1);
    if (!g_live) {
        printf("FAIL: host calloc for live model\n");
        exit(2);
    }
    be_init(ARENA_BASE, g_managed, meta);
    check(be_free_count() == g_managed, "init free count");
}

/* W1: drain + refill (raw throughput). */
static void workload_drain(double *alloc_ms, double *free_ms) {
    uint32_t *addrs = (uint32_t *)malloc((size_t)g_managed * 4u);
    double t0 = now_ms();
    uint32_t n = 0, a;
    while ((a = be_alloc()) != 0) {
        check(a >= ARENA_BASE && ((a - ARENA_BASE) % NOVA_FRAME_SIZE) == 0,
              "W1 addr range/align");
        check(idx_of(a) < g_managed && g_live[idx_of(a)] == 0,
              "W1 double-alloc");
        g_live[idx_of(a)] = 1;
        addrs[n++] = a;
    }
    double t1 = now_ms();
    check(n == g_managed, "W1 drained all");
    check(be_alloc() == 0, "W1 OOM sticks");
    check(be_free_count() == 0, "W1 zero free");
    for (uint32_t i = n; i > 0; i--) {
        uint32_t id = idx_of(addrs[i - 1]);
        check(g_live[id] == 1, "W1 free of live");
        g_live[id] = 0;
        be_free(addrs[i - 1]);
    }
    double t2 = now_ms();
    check(be_free_count() == g_managed, "W1 full restore");
    free(addrs);
    *alloc_ms = t1 - t0;
    *free_ms = t2 - t1;
}

/* W2: 200k-op churn with capped live set. Records largest free run
 * BEFORE cleanup (fragmentation signal under random churn). */
static void workload_churn(double *ms, uint32_t *largest) {
    uint32_t *live = (uint32_t *)malloc(8192u * 4u);
    uint32_t live_n = 0;
    srand(20260929);
    double t0 = now_ms();
    for (int op = 0; op < 200000; op++) {
        if (live_n == 0 || (rand() < RAND_MAX / 2 && live_n < 8192u)) {
            uint32_t a = be_alloc();
            check(a != 0, "W2 unexpected OOM");
            if (a == 0) {
                break;
            }
            check(g_live[idx_of(a)] == 0, "W2 double-alloc");
            g_live[idx_of(a)] = 1;
            live[live_n++] = a;
        } else {
            uint32_t s = (uint32_t)rand() % live_n;
            uint32_t a = live[s];
            live[s] = live[--live_n];
            check(g_live[idx_of(a)] == 1, "W2 free of dead");
            g_live[idx_of(a)] = 0;
            be_free(a);
        }
        if ((op & 4095) == 0) {
            check(be_free_count() + live_n == g_managed, "W2 count invariant");
        }
    }
    *largest = be_largest_free();
    check(*largest > 0 && *largest <= be_free_count(), "W2 largest sane");
    for (uint32_t i = 0; i < live_n; i++) {
        g_live[idx_of(live[i])] = 0;
        be_free(live[i]);
    }
    double t1 = now_ms();
    check(be_free_count() == g_managed, "W2 full restore");
    free(live);
    *ms = t1 - t0;
}

/* W3: fragment, then measure absorption + largest run. */
static void workload_frag(uint32_t *secondary, uint32_t *largest) {
    uint32_t *first = (uint32_t *)malloc(20000u * 4u);
    uint32_t *rec = (uint32_t *)malloc((size_t)g_managed * 4u);
    for (int i = 0; i < 20000; i++) {
        uint32_t a = be_alloc();
        check(a != 0, "W3 initial alloc");
        g_live[idx_of(a)] = 1;
        first[i] = a;
    }
    for (int i = 0; i < 20000; i += 3) {
        g_live[idx_of(first[i])] = 0;
        be_free(first[i]);
        first[i] = 0;
    }
    uint32_t m = 0, a;
    while ((a = be_alloc()) != 0) {
        g_live[idx_of(a)] = 1;
        rec[m++] = a;
        if (m >= g_managed) {
            break;
        }
    }
    *secondary = m;
    *largest = be_largest_free();
    check(*largest <= be_free_count() &&
          (be_free_count() > 0 ? *largest > 0 : *largest == 0),
          "W3 largest sane");
    for (uint32_t i = 0; i < m; i++) {
        g_live[idx_of(rec[i])] = 0;
        be_free(rec[i]);
    }
    for (int i = 0; i < 20000; i++) {
        if (first[i] != 0) {
            g_live[idx_of(first[i])] = 0;
            be_free(first[i]);
        }
    }
    free(rec);
    free(first);
    check(be_free_count() == g_managed, "W3 full restore");
}

int main(void) {
    double w1a, w1b, w2;
    uint32_t secondary, largest, churn_largest;
    setup_arena();
    printf("backend=%s managed=%lu frames\n", be_name(),
           (unsigned long)g_managed);
    workload_drain(&w1a, &w1b);
    printf("W1 drain: alloc %.1f ms (%.0f ops/s), free %.1f ms (%.0f ops/s)\n",
           w1a, g_managed * 1000.0 / (w1a + 0.001),
           w1b, g_managed * 1000.0 / (w1b + 0.001));
    workload_churn(&w2, &churn_largest);
    printf("W2 churn: 200000 ops in %.1f ms (%.0f ops/s), largest_free=%lu\n",
           w2, 200000 * 1000.0 / (w2 + 0.001),
           (unsigned long)churn_largest);
    setup_arena();
    workload_frag(&secondary, &largest);
    printf("W3 frag: secondary allocs=%lu largest_free=%lu frames\n",
           (unsigned long)secondary, (unsigned long)largest);
    if (!g_failed) {
        printf("VERDICT: %s correct\n", be_name());
    }
    return g_failed ? 1 : 0;
}
