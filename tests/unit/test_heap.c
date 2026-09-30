/* NOVA OS heap host test (Phase 2c, T7).
 * Links the REAL heap.c + pmm.c + pmm_bitmap.c against a fake boot info
 * (one 64MB region) and a malloc'd backing arena. The heap's HPTR
 * translation makes guest-physical IDs work on host pointers. Reference
 * live-set model + canaries + exact PMM accounting. Exit 0 iff green.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "mm/heap.h"
#include "mm/pmm.h"
#include "boot/boot_info.h"

#define BACKING_FRAMES 16384u /* 64MB */

static int g_failed;

__attribute__((noreturn)) void nova_panic(const char *reason) {
    printf("FAIL: heap panicked: %s\n", reason);
    exit(1);
}

static void check(int cond, const char *what) {
    if (!cond) {
        printf("FAIL: %s\n", what);
        g_failed = 1;
    }
}

static double now_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static nova_mem_region_t g_regions[1];
static nova_boot_info_t g_info;

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* crash still leaves output */
    void *backing = malloc((size_t)BACKING_FRAMES * 4096u);
    if (!backing) {
        printf("FAIL: host malloc backing\n");
        return 2;
    }
    g_regions[0].base = 0x100000ull;
    g_regions[0].length = (uint64_t)BACKING_FRAMES * 4096u;
    g_regions[0].type = 1;
    g_regions[0].reserved = 0;
    g_info.magic = NOVA_BOOT_MAGIC;
    g_info.version = NOVA_BOOT_INFO_VERSION;
    g_info.flags = 0;
    g_info.cmdline = 0;
    g_info.regions = g_regions;
    g_info.region_count = 1;

    kheap_set_translation((uintptr_t)backing - 0x100000u);
    uint32_t meta_need = be_meta_needed(BACKING_FRAMES);
    void *meta = malloc(meta_need);
    if (!meta) {
        printf("FAIL: host malloc meta\n");
        return 2;
    }
    if (pmm_init(&g_info, meta, meta_need) != 0) {
        printf("FAIL: pmm_init\n");
        return 1;
    }
    kheap_init();
    uint32_t free0 = pmm_free_count();
    printf("heap-host: arena frames=%lu free=%lu\n",
           (unsigned long)pmm_total_frames(), (unsigned long)free0);

    /* Mixed sizes incl. edges + large, canary-checked. */
    void *a[512];
    for (uint32_t i = 0; i < 512u; i++) {
        uint32_t sz = 1u + (i * 53u) % 3000u;
        a[i] = kmalloc(sz);
        check(a[i] != 0, "alloc non-null");
        check(((uintptr_t)a[i] & 7u) == 0, "8-byte aligned");
        *(uint32_t *)a[i] = (uint32_t)(uintptr_t)a[i] ^ 0x5A5A5A5Au;
    }
    check(kmalloc(0) == 0, "kmalloc(0) NULL");
    void *l1 = kmalloc(5000u);
    void *l2 = kmalloc(1048576u);
    check(l1 != 0 && l2 != 0, "large allocs");
    *(uint32_t *)l1 = 0x11111111u;
    *(uint32_t *)l2 = 0x22222222u;
    for (uint32_t i = 0; i < 512u; i++) {
        check(*(uint32_t *)a[i] ==
              ((uint32_t)(uintptr_t)a[i] ^ 0x5A5A5A5Au), "canary intact");
    }

    /* Churn with live-set reference (cap 2048). */
    void *live[2048];
    uint32_t live_n = 0;
    srand(424242);
    double t0 = now_ms();
    for (int op = 0; op < 50000; op++) {
        if (live_n == 0 || (rand() < RAND_MAX / 2 && live_n < 2048u)) {
            uint32_t sz = 1u + (uint32_t)rand() % 4000u;
            if ((rand() % 50) == 0) {
                sz = 5000u + (uint32_t)rand() % 60000u;
            }
            void *p = kmalloc(sz);
            check(p != 0, "churn OOM");
            if (!p) {
                break;
            }
            int dup = 0;
            for (uint32_t k = 0; k < live_n; k++) {
                if (live[k] == p) {
                    dup = 1;
                }
            }
            check(!dup, "churn double-alloc");
            *(uint32_t *)p = (uint32_t)(uintptr_t)p ^ 0x3C3C3C3Cu;
            live[live_n++] = p;
        } else {
            uint32_t s = (uint32_t)rand() % live_n;
            void *p = live[s];
            live[s] = live[--live_n];
            check(*(uint32_t *)p ==
                  ((uint32_t)(uintptr_t)p ^ 0x3C3C3C3Cu), "churn canary");
            kfree(p);
        }
    }
    double t1 = now_ms();
    for (uint32_t i = 0; i < live_n; i++) {
        kfree(live[i]);
    }
    for (uint32_t i = 0; i < 512u; i++) {
        kfree(a[i]);
    }
    kfree(l1);
    kfree(l2);
    printf("churn: 50000 ops in %.1f ms (%.0f ops/s)\n",
           t1 - t0, 50000 * 1000.0 / ((t1 - t0) + 0.001));

    kheap_trim();
    uint32_t slabs, live_objs, large;
    kheap_stats(&slabs, &live_objs, &large);
    printf("after: slabs=%lu live=%lu large=%luB pmm_free=%lu\n",
           (unsigned long)slabs, (unsigned long)live_objs,
           (unsigned long)large, (unsigned long)pmm_free_count());
    check(live_objs == 0 && large == 0, "zero live");
    check(pmm_free_count() == free0, "exact PMM restore");

    if (kheap_selftest() != 0) {
        printf("FAIL: kheap_selftest\n");
        g_failed = 1;
    } else {
        printf("[pass] kheap_selftest\n");
    }
    if (!g_failed) {
        printf("VERDICT: heap correct\n");
    }
    return g_failed ? 1 : 0;
}
