/* NOVA OS bitmap frame allocator (Phase 2a candidate).
 * Next-fit cursor with whole-word skipping: free words are skipped 32
 * frames at a time, so drain cost is O(n^2/32), not O(n^2). Double-free
 * and foreign addresses are ignored (caller contract, verified by the
 * reference model in benchmarks/pmm/bench.c and pmm_selftest).
 */
#include <stdint.h>
#include <stddef.h>

#include "pmm.h"

static uint32_t *g_bits;
static uint32_t g_base, g_n, g_free, g_cursor;

uint32_t be_meta_needed(uint32_t nframes) {
    return (nframes + 7u) / 8u;
}

void be_init(uint32_t base_addr, uint32_t nframes, void *meta) {
    g_base = base_addr;
    g_n = nframes;
    g_bits = (uint32_t *)meta;
    g_free = nframes;
    g_cursor = 0;
    for (uint32_t i = 0, words = (nframes + 31u) / 32u; i < words; i++) {
        g_bits[i] = 0;
    }
}

static uint32_t bit_is_set(uint32_t i) {
    return (g_bits[i >> 5] >> (i & 31u)) & 1u;
}

/* First free frame at or after s (no wraparound); -1 if none. */
static int32_t scan_from(uint32_t s) {
    uint32_t words = (g_n + 31u) / 32u;
    uint32_t w = s >> 5;
    uint32_t b = s & 31u;
    if (w < words) {
        uint32_t bits = g_bits[w] | (b == 0 ? 0u : ((1u << b) - 1u));
        if (~bits != 0u) {
            uint32_t i = (w << 5) + __builtin_ctz(~bits);
            if (i < g_n) {
                return (int32_t)i;
            }
        }
        for (w++; w < words; w++) {
            bits = g_bits[w];
            if (w == words - 1 && (g_n & 31u) != 0) {
                bits |= ~0u << (g_n & 31u); /* tail beyond n reads used */
            }
            if (~bits != 0u) {
                return (int32_t)((w << 5) + __builtin_ctz(~bits));
            }
        }
    }
    return -1;
}

uint32_t be_alloc(void) {
    if (g_free == 0) {
        return 0;
    }
    int32_t i = scan_from(g_cursor);
    if (i < 0) {
        i = scan_from(0);
    }
    if (i < 0) {
        return 0;
    }
    g_bits[(uint32_t)i >> 5] |= 1u << ((uint32_t)i & 31u);
    g_free--;
    g_cursor = (uint32_t)i + 1u;
    if (g_cursor >= g_n) {
        g_cursor = 0;
    }
    return g_base + (uint32_t)i * NOVA_FRAME_SIZE;
}

void be_free(uint32_t addr) {
    if (addr < g_base) {
        return;
    }
    uint32_t i = (addr - g_base) / NOVA_FRAME_SIZE;
    if (i < g_n && bit_is_set(i)) {
        g_bits[i >> 5] &= ~(1u << (i & 31u));
        g_free++;
    }
}

uint32_t be_alloc_contig(uint32_t n) {
    if (n == 0 || n > g_free) {
        return 0;
    }
    uint32_t run = 0;
    for (uint32_t i = 0; i < g_n; i++) {
        if (!bit_is_set(i)) {
            if (++run == n) {
                uint32_t b = i - n + 1u;
                for (uint32_t k = 0; k < n; k++) {
                    g_bits[(b + k) >> 5] |= 1u << ((b + k) & 31u);
                }
                g_free -= n;
                g_cursor = b + n;
                if (g_cursor >= g_n) {
                    g_cursor = 0;
                }
                return g_base + b * NOVA_FRAME_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

void be_free_contig(uint32_t addr, uint32_t n) {
    if (addr < g_base) {
        return;
    }
    uint32_t b = (addr - g_base) / NOVA_FRAME_SIZE;
    if (b + n > g_n) {
        return;
    }
    uint32_t cleared = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (bit_is_set(b + k)) {
            g_bits[(b + k) >> 5] &= ~(1u << ((b + k) & 31u));
            cleared++;
        }
    }
    g_free += cleared;
}

uint32_t be_free_count(void) {
    return g_free;
}

uint32_t be_largest_free(void) {
    uint32_t best = 0, run = 0;
    for (uint32_t i = 0; i < g_n; i++) {
        if (!bit_is_set(i)) {
            if (++run > best) {
                best = run;
            }
        } else {
            run = 0;
        }
    }
    return best;
}

const char *be_name(void) {
    return "bitmap";
}
