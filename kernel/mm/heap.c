/* NOVA OS kernel heap: slabs + large runs (Phase 2c).
 *
 * Every PMM frame the heap owns carries a tag (carved 1B/frame table):
 * 0=other, 1=slab, 2=large run, 3=heap metadata. kfree dispatches on the
 * tag and verifies magic/range/alignment, so corrupt, foreign, and
 * double-freed pointers panic instead of corrupting state (slab
 * double-free is caught by a freelist walk on every free).
 *
 * All frame addresses cross HPTR (identity on the guest, a real offset
 * in the host test), keeping this file pointer-width clean for the
 * future higher-half kernel.
 */
#include <stdint.h>
#include <stddef.h>

#include "pmm.h"
#include "panic.h"
#include "heap.h"

#define HEAP_CLASSES 9u
static const uint32_t HEAP_SIZES[HEAP_CLASSES] = {
    8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u
};
#define HEAP_MAX_CLASS 2048u
#define SLAB_MAGIC 0x534C4142u
#define LARGE_MAGIC 0x4C415247u
#define SLAB_OBJS_OFF 32u

#define TAG_OTHER 0u
#define TAG_SLAB 1u
#define TAG_LARGE 2u
#define TAG_META 3u

typedef struct slab {
    uint32_t magic;
    void *freelist;
    struct slab *next;
    uint16_t total;
    uint16_t nfree;
    uint8_t cls;
    uint8_t _pad[15];
} slab_t;

typedef struct {
    slab_t *partial;
    uint32_t slabs;
    uint32_t live;
} cache_t;

typedef struct {
    uint32_t magic;
    uint32_t frames;
} large_t;

static cache_t g_cache[HEAP_CLASSES];
static uint8_t *g_tags;
static uint32_t g_nframes;
static uint32_t g_large_bytes;
static uintptr_t g_offset;

void kheap_set_translation(uintptr_t offset) {
    g_offset = offset;
}

static inline void *HPTR(uint32_t phys) {
    return (void *)(g_offset + (uintptr_t)phys);
}

static inline uint32_t HPHYS(const void *p) {
    return (uint32_t)((uintptr_t)p - g_offset);
}

static uint32_t frame_index(uint32_t phys, uint32_t *base_out) {
    uint32_t base = pmm_arena_base();
    if (phys < base) {
        nova_panic("heap-bad-free");
    }
    uint32_t idx = (phys - base) / NOVA_FRAME_SIZE;
    if (idx >= g_nframes) {
        nova_panic("heap-bad-free");
    }
    if (base_out) {
        *base_out = base;
    }
    return idx;
}

void kheap_init(void) {
    uint32_t n = pmm_total_frames();
    uint32_t tag_frames = (n + NOVA_FRAME_SIZE - 1u) / NOVA_FRAME_SIZE;
    uint32_t tphys = pmm_alloc_contig(tag_frames);
    if (tphys == 0) {
        nova_panic("heap-no-tagmem");
    }
    g_tags = (uint8_t *)HPTR(tphys);
    g_nframes = n;
    for (uint32_t i = 0; i < n; i++) {
        g_tags[i] = TAG_OTHER;
    }
    uint32_t base = pmm_arena_base();
    uint32_t tidx = (tphys - base) / NOVA_FRAME_SIZE;
    for (uint32_t k = 0; k < tag_frames; k++) {
        g_tags[tidx + k] = TAG_META;
    }
    g_large_bytes = 0;
}

static int class_of(uint32_t size) {
    for (uint32_t i = 0; i < HEAP_CLASSES; i++) {
        if (size <= HEAP_SIZES[i]) {
            return (int)i;
        }
    }
    return -1;
}

static slab_t *slab_new(uint32_t cls) {
    uint32_t phys = pmm_alloc_frame();
    if (phys == 0) {
        return 0;
    }
    uint32_t base = pmm_arena_base();
    g_tags[(phys - base) / NOVA_FRAME_SIZE] = TAG_SLAB;
    slab_t *s = (slab_t *)HPTR(phys);
    uint32_t size = HEAP_SIZES[cls];
    uint32_t total = (NOVA_FRAME_SIZE - SLAB_OBJS_OFF) / size;
    s->magic = SLAB_MAGIC;
    s->cls = (uint8_t)cls;
    s->total = (uint16_t)total;
    s->nfree = (uint16_t)total;
    s->next = g_cache[cls].partial;
    g_cache[cls].partial = s;
    g_cache[cls].slabs++;
    /* Slots built forward from SLAB_OBJS_OFF so every slot satisfies
     * (slot - (frame+32)) % size == 0 (required by kfree_slab). Push in
     * reverse so the lowest address allocates first. */
    s->freelist = 0;
    for (uint32_t k = total; k > 0; k--) {
        uintptr_t obj = (uintptr_t)s + SLAB_OBJS_OFF + (k - 1u) * size;
        *(void **)obj = s->freelist;
        s->freelist = (void *)obj;
    }
    return s;
}

static void *kmalloc_large(uint32_t size) {
    uint32_t frames = (size + (uint32_t)sizeof(large_t) +
                       NOVA_FRAME_SIZE - 1u) / NOVA_FRAME_SIZE;
    uint32_t phys = pmm_alloc_contig(frames);
    if (phys == 0) {
        return 0;
    }
    uint32_t base = pmm_arena_base();
    uint32_t idx = (phys - base) / NOVA_FRAME_SIZE;
    for (uint32_t k = 0; k < frames; k++) {
        g_tags[idx + k] = TAG_LARGE;
    }
    large_t *h = (large_t *)HPTR(phys);
    h->magic = LARGE_MAGIC;
    h->frames = frames;
    g_large_bytes += frames * NOVA_FRAME_SIZE;
    return (void *)((uintptr_t)h + sizeof(large_t));
}

void *kmalloc(uint32_t size) {
    if (size == 0) {
        return 0;
    }
    int cls = class_of(size);
    if (cls < 0) {
        return kmalloc_large(size);
    }
    cache_t *c = &g_cache[(uint32_t)cls];
    if (c->partial == 0) {
        if (slab_new((uint32_t)cls) == 0) {
            return 0;
        }
    }
    slab_t *s = c->partial;
    void *p = s->freelist;
    s->freelist = *(void **)p;
    s->nfree--;
    c->live++;
    if (s->nfree == 0) {
        c->partial = s->next; /* full: unlink (s->next left stale) */
    }
    return p;
}

static void kfree_slab(void *ptr, uint32_t idx, uint32_t base) {
    slab_t *s = (slab_t *)HPTR(base + idx * NOVA_FRAME_SIZE);
    if (s->magic != SLAB_MAGIC || s->cls >= HEAP_CLASSES) {
        nova_panic("heap-bad-slab");
    }
    uint32_t size = HEAP_SIZES[s->cls];
    uintptr_t lo = (uintptr_t)s + SLAB_OBJS_OFF;
    uintptr_t hi = (uintptr_t)s + NOVA_FRAME_SIZE;
    uintptr_t p = (uintptr_t)ptr;
    if (p < lo || p >= hi || ((p - lo) % size) != 0) {
        nova_panic("heap-bad-slab-ptr");
    }
    for (void *f = s->freelist; f != 0; f = *(void **)f) {
        if (f == ptr) {
            nova_panic("heap-double-free");
        }
    }
    cache_t *c = &g_cache[s->cls];
    uint32_t was_free = s->nfree;
    *(void **)ptr = s->freelist;
    s->freelist = ptr;
    s->nfree++;
    c->live--;
    /* List invariant: partial holds non-full slabs plus at most one full
     * spare. was_free==0 means full, hence not in the list. */
    if (s->nfree == s->total) {
        if (c->slabs > 1) {
            slab_t **link = &c->partial;
            while (*link != 0 && *link != s) {
                link = &(*link)->next;
            }
            if (*link != 0) {
                *link = s->next;
            }
            g_tags[idx] = TAG_OTHER;
            pmm_free_frame(base + idx * NOVA_FRAME_SIZE);
            c->slabs--;
        } else if (was_free == 0) {
            s->next = c->partial;
            c->partial = s;
        }
    } else if (was_free == 0) {
        s->next = c->partial;
        c->partial = s;
    }
}

static void kfree_large(void *ptr, uint32_t idx, uint32_t base) {
    large_t *h = (large_t *)((uintptr_t)ptr - sizeof(large_t));
    if (h->magic != LARGE_MAGIC || h->frames == 0 ||
        idx + h->frames > g_nframes) {
        nova_panic("heap-bad-large");
    }
    uint32_t run = base + idx * NOVA_FRAME_SIZE;
    for (uint32_t k = 0; k < h->frames; k++) {
        g_tags[idx + k] = TAG_OTHER;
    }
    g_large_bytes -= h->frames * NOVA_FRAME_SIZE;
    pmm_free_contig(run, h->frames);
}

void kfree(void *ptr) {
    uint32_t base;
    uint32_t idx;
    if (ptr == 0) {
        return;
    }
    idx = frame_index(HPHYS(ptr), &base);
    uint8_t t = g_tags[idx];
    if (t == TAG_SLAB) {
        kfree_slab(ptr, idx, base);
    } else if (t == TAG_LARGE) {
        kfree_large(ptr, idx, base);
    } else {
        nova_panic("heap-bad-free");
    }
}

int kheap_trim(void) {
    /* Release every fully-free slab (no spare retention): after a trim,
     * only live slabs and heap metadata hold frames, so PMM accounting
     * is exact. (kfree separately keeps one warm spare per cache.) */
    int released = 0;
    for (uint32_t c = 0; c < HEAP_CLASSES; c++) {
        slab_t **link = &g_cache[c].partial;
        while (*link != 0) {
            slab_t *s = *link;
            if (s->nfree == s->total) {
                *link = s->next;
                /* Pointer-width discipline: arithmetic in uintptr_t,
                 * cast only after dividing down to a frame index. */
                uint32_t idx = (uint32_t)(((uintptr_t)s - g_offset -
                                           pmm_arena_base()) /
                                          NOVA_FRAME_SIZE);
                if (idx >= g_nframes) {
                    nova_panic("heap-trim-range");
                }
                g_tags[idx] = TAG_OTHER;
                pmm_free_frame(pmm_arena_base() + idx * NOVA_FRAME_SIZE);
                g_cache[c].slabs--;
                released++;
            } else {
                link = &s->next;
            }
        }
    }
    return released;
}

void kheap_stats(uint32_t *slabs_out, uint32_t *live_out,
                 uint32_t *large_bytes_out) {
    uint32_t slabs = 0, live = 0;
    for (uint32_t c = 0; c < HEAP_CLASSES; c++) {
        slabs += g_cache[c].slabs;
        live += g_cache[c].live;
    }
    if (slabs_out) {
        *slabs_out = slabs;
    }
    if (live_out) {
        *live_out = live;
    }
    if (large_bytes_out) {
        *large_bytes_out = g_large_bytes;
    }
}

int kheap_selftest(void) {
    uint32_t free0 = pmm_free_count();
    void *a[256];
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t sz = 1u + (i * 37u) % HEAP_MAX_CLASS;
        a[i] = kmalloc(sz);
        if (a[i] == 0) {
            return -1;
        }
        *(uint32_t *)a[i] = (uint32_t)(uintptr_t)a[i] ^ 0xA5A5A5A5u;
    }
    if (kmalloc(0) != 0) {
        return -2;
    }
    void *l1 = kmalloc(5000u);
    void *l2 = kmalloc(100000u);
    if (l1 == 0 || l2 == 0) {
        return -3;
    }
    *(uint32_t *)l1 = 0xDEADBEEFu;
    *(uint32_t *)l2 = 0xCAFEBABEu;
    for (uint32_t i = 0; i < 256u; i++) {
        if (*(uint32_t *)a[i] != ((uint32_t)(uintptr_t)a[i] ^ 0xA5A5A5A5u)) {
            return -4;
        }
    }
    if (*(uint32_t *)l1 != 0xDEADBEEFu ||
        *(uint32_t *)l2 != 0xCAFEBABEu) {
        return -5;
    }
    for (uint32_t i = 0; i < 256u; i += 2) {
        kfree(a[i]);
        a[i] = 0;
    }
    void *b[128];
    for (uint32_t i = 0; i < 128u; i++) {
        b[i] = kmalloc(64u);
        if (b[i] == 0) {
            return -6;
        }
    }
    kfree(l1);
    kfree(l2);
    for (uint32_t i = 1; i < 256u; i += 2) {
        kfree(a[i]);
    }
    for (uint32_t i = 0; i < 128u; i++) {
        kfree(b[i]);
    }
    kheap_trim();
    uint32_t slabs, live, large;
    kheap_stats(&slabs, &live, &large);
    if (live != 0 || large != 0) {
        return -7;
    }
    if (pmm_free_count() != free0) {
        return -8;
    }
    return 0;
}
