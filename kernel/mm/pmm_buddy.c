/* NOVA OS buddy frame allocator (Phase 2a candidate).
 * Single arena, orders 0..18 (4KB..1GB). Intrusive free lists (next index
 * stored in the free block itself) plus two carved arrays: ord[] (order
 * of a free head, 0xFF otherwise) and an alloc bitmap (live order-0
 * frames; also marks padding beyond the managed range so coalescing
 * stops at the edge). Invariant: ord[i]==k iff block [i,i+2^k) is on
 * free list k; every list mutation goes through list_add/list_remove.
 * be_alloc always returns order-0; larger requests are out of scope.
 */
#include <stdint.h>
#include <stddef.h>

#include "pmm.h"

#define BE_MAX_ORDER 18

static int32_t g_head[BE_MAX_ORDER + 1];
static int32_t *g_next;
static uint8_t *g_ord;
static uint8_t *g_used;
static uint32_t g_base, g_total, g_n, g_free;

static uint32_t roundup_pow2(uint32_t v) {
    uint32_t t = 1;
    while (t < v) {
        t <<= 1;
    }
    return t;
}

uint32_t be_meta_needed(uint32_t nframes) {
    uint32_t t = roundup_pow2(nframes);
    return t * 4u + t + (t + 7u) / 8u;
}

static void list_add(uint32_t i, uint32_t k) {
    g_next[i] = g_head[k];
    g_head[k] = (int32_t)i;
    g_ord[i] = (uint8_t)k;
}

static void list_remove(uint32_t i, uint32_t k) {
    int32_t *link = &g_head[k];
    while (*link != -1 && (uint32_t)*link != i) {
        link = &g_next[*link];
    }
    if (*link != -1) {
        *link = g_next[i];
        g_ord[i] = 0xFF;
    }
}

static int used_bit(uint32_t i) {
    return (g_used[i >> 3] >> (i & 7u)) & 1;
}

static void used_set(uint32_t i) {
    g_used[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static void used_clear(uint32_t i) {
    g_used[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}

void be_init(uint32_t base_addr, uint32_t nframes, void *meta) {
    g_base = base_addr;
    g_n = nframes;
    g_total = roundup_pow2(nframes);
    g_next = (int32_t *)meta;
    g_ord = (uint8_t *)((char *)meta + (size_t)g_total * 4u);
    g_used = g_ord + g_total;
    for (uint32_t k = 0; k <= BE_MAX_ORDER; k++) {
        g_head[k] = -1;
    }
    for (uint32_t i = 0; i < g_total; i++) {
        g_ord[i] = 0xFF;
    }
    for (uint32_t i = 0; i < (g_total + 7u) / 8u; i++) {
        g_used[i] = 0;
    }
    for (uint32_t i = g_n; i < g_total; i++) {
        used_set(i);
    }
    g_free = 0;
    /* Greedy maximal decomposition of [0, n). */
    uint32_t off = 0, rem = nframes;
    while (rem > 0) {
        uint32_t k = 0;
        while (k < BE_MAX_ORDER && (off & ((1u << (k + 1)) - 1u)) == 0 &&
               (1u << (k + 1)) <= rem) {
            k++;
        }
        list_add(off, k);
        g_free += 1u << k;
        off += 1u << k;
        rem -= 1u << k;
    }
}

uint32_t be_alloc(void) {
    uint32_t k = 0;
    while (k <= BE_MAX_ORDER && g_head[k] == -1) {
        k++;
    }
    if (k > BE_MAX_ORDER) {
        return 0;
    }
    uint32_t i = (uint32_t)g_head[k];
    g_head[k] = g_next[i];
    g_ord[i] = 0xFF;
    while (k > 0) {
        k--;
        list_add(i + (1u << k), k);
    }
    used_set(i);
    g_free--;
    return g_base + i * NOVA_FRAME_SIZE;
}

void be_free(uint32_t addr) {
    if (addr < g_base) {
        return;
    }
    uint32_t i = (addr - g_base) / NOVA_FRAME_SIZE;
    if (i >= g_n || !used_bit(i)) {
        return; /* double-free / foreign: ignore */
    }
    used_clear(i);
    uint32_t k = 0;
    while (k < BE_MAX_ORDER) {
        uint32_t b = i ^ (1u << k);
        if (b >= g_total || g_ord[b] != (uint8_t)k || used_bit(b)) {
            break;
        }
        list_remove(b, k);
        i = (i < b) ? i : b;
        k++;
    }
    list_add(i, k);
    g_free++;
}

uint32_t be_alloc_contig(uint32_t nframes) {
    (void)nframes;
    return 0; /* explicitly unimplemented (see pmm.h) */
}

void be_free_contig(uint32_t addr, uint32_t nframes) {
    (void)addr;
    (void)nframes; /* explicitly unimplemented (see pmm.h) */
}

uint32_t be_free_count(void) {
    return g_free;
}

uint32_t be_largest_free(void) {
    for (uint32_t k = BE_MAX_ORDER + 1u; k > 0; k--) {
        if (g_head[k - 1] != -1) {
            return 1u << (k - 1);
        }
    }
    return 0;
}

const char *be_name(void) {
    return "buddy";
}
