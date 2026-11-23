/* NOVA OS PMM front (Phase 2a, guest side).
 * Arena policy: the largest type-1 E820 region at/above 1MB (on the
 * 1GB test box: 0x100000..0x3FFF0000). Everything below 1MB stays
 * reserved (BIOS, VGA, stage1/2, boot info, stacks). Other usable
 * regions are ignored for now: multi-zone support is explicit future
 * work, not silent cheating (see ADR-0010). Backend metadata is carved
 * from the TOP of the arena, so the kernel image carries no per-frame
 * tables (keeps the INT13 single-load budget intact).
 */
#include <stdint.h>
#include <stddef.h>

#include "pmm.h"
#include "addrspace.h"

static uint32_t g_base, g_end, g_managed;
static void *g_meta;
static uint32_t g_allocs, g_frees;

int pmm_init(const nova_boot_info_t *info, void *meta_buf,
             uint32_t meta_len) {
    uint64_t best_base = 0, best_len = 0;
    const uint64_t limit = (uint64_t)KERNEL_PDS << 22;
    const uint64_t mask = NOVA_FRAME_SIZE - 1u;
    if (!nova_boot_info_valid(info)) {
        return -1;
    }
    for (uint32_t i = 0; i < info->region_count; i++) {
        if (info->regions[i].length > UINT64_MAX - info->regions[i].base) {
            return -1;
        }
    }
    for (uint32_t i = 0; i < info->region_count; i++) {
        const nova_mem_region_t *r = &info->regions[i];
        if (r->type != 1 || r->base >= limit) {
            continue;
        }
        uint64_t base = r->base < NOVA_ARENA_MIN ? NOVA_ARENA_MIN : r->base;
        uint64_t end = r->base + r->length;
        base = (base + mask) & ~mask;
        end = (end < limit ? end : limit) & ~mask;
        if (end <= base || end - base <= best_len) {
            continue;
        }
        /* Single-arena policy: skip ambiguous RAM rather than using any
         * pages that firmware also describes as reserved. */
        uint32_t j;
        for (j = 0; j < info->region_count; j++) {
            const nova_mem_region_t *other = &info->regions[j];
            if (other->type != 1 && other->length && other->base < end &&
                other->base + other->length > base) {
                break;
            }
        }
        if (j == info->region_count) {
            best_base = base;
            best_len = end - base;
        }
    }
    if (best_len < 0x200000ull) {
        return -1; /* need at least a 2MB arena */
    }
    uint32_t base = (uint32_t)best_base;
    uint32_t frames = (uint32_t)(best_len / NOVA_FRAME_SIZE);
    uint32_t need = be_meta_needed(frames);
    uint32_t managed = frames;
    void *meta = meta_buf;
    if (meta == 0) {
        uint32_t meta_frames =
            (need + NOVA_FRAME_SIZE - 1u) / NOVA_FRAME_SIZE;
        if (meta_frames >= frames) {
            return -2;
        }
        managed = frames - meta_frames;
        meta = (void *)(uintptr_t)(base + managed * NOVA_FRAME_SIZE);
    } else {
        if (meta_len < be_meta_needed(managed)) {
            return -3;
        }
    }
    be_init(base, managed, meta);
    g_base = base;
    g_end = (uint32_t)(best_base + best_len);
    g_managed = managed;
    g_meta = meta;
    g_allocs = g_frees = 0;
    return 0;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t a = be_alloc();
    if (a != 0) {
        g_allocs++;
    }
    return a;
}

void pmm_free_frame(uint32_t a) {
    if (a != 0) {
        be_free(a);
        g_frees++;
    }
}

uint32_t pmm_alloc_contig(uint32_t nframes) {
    return be_alloc_contig(nframes);
}

void pmm_free_contig(uint32_t a, uint32_t nframes) {
    if (a != 0 && nframes != 0) {
        be_free_contig(a, nframes);
    }
}

uint32_t pmm_free_count(void) {
    return be_free_count();
}

uint32_t pmm_largest_free(void) {
    return be_largest_free();
}

uint32_t pmm_total_frames(void) {
    return g_managed;
}

uint32_t pmm_arena_base(void) {
    return g_base;
}

uint32_t pmm_arena_end(void) {
    return g_end;
}

int pmm_selftest(void) {
    if (be_free_count() != g_managed) {
        return -1;
    }
    /* Drain everything; no 1MB side table needed (re-init, boot-only). */
    uint32_t n = 0, a;
    while ((a = be_alloc()) != 0) {
        n++;
    }
    if (n != g_managed || be_alloc() != 0 || be_free_count() != 0) {
        return -2;
    }
    be_init(g_base, g_managed, g_meta);
    g_allocs = g_frees = 0;
    if (be_free_count() != g_managed) {
        return -3;
    }
    /* Bounded interleaved pattern on the stack (3KB). */
    uint32_t b1[512], b2[256];
    for (uint32_t i = 0; i < 512; i++) {
        a = be_alloc();
        if (a == 0) {
            return -4;
        }
        b1[i] = a;
    }
    for (uint32_t i = 0; i < 512; i += 2) {
        be_free(b1[i]);
    }
    for (uint32_t i = 0; i < 256; i++) {
        a = be_alloc();
        if (a == 0) {
            return -5;
        }
        b2[i] = a;
    }
    for (uint32_t i = 1; i < 512; i += 2) {
        be_free(b1[i]);
    }
    for (uint32_t i = 0; i < 256; i++) {
        be_free(b2[i]);
    }
    if (be_free_count() != g_managed) {
        return -6;
    }
    uint32_t largest = be_largest_free();
    if (largest == 0 || largest > g_managed) {
        return -7;
    }
    return 0;
}
