/* NOVA OS kernel heap interface (Phase 2c).
 * Slab caches for <=2048 bytes (8..2048 size classes, 8-byte aligned),
 * contiguous PMM runs with headers for larger sizes. NULL on OOM
 * (kmalloc(0) also returns NULL). kfree(NULL) is a no-op. Corrupt or
 * foreign pointers panic (loud, not silent). Single-core: no locking
 * yet (SMP adds it with the scheduler).
 */
#ifndef NOVA_HEAP_H
#define NOVA_HEAP_H

#include <stdint.h>

void kheap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);
int kheap_selftest(void);
int kheap_trim(void);
void kheap_stats(uint32_t *slabs_out, uint32_t *live_out,
                 uint32_t *large_bytes_out);
/* Host-test hook: frame translation offset (guest identity == 0, never
 * call there). Must be set before kheap_init when phys != virt. */
void kheap_set_translation(uintptr_t offset);

#endif /* NOVA_HEAP_H */
