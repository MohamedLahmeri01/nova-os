#include "memory_map.h"

/* Read little-endian UEFI descriptor fields with no alignment assumption. */
static uint32_t load32(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8u * i);
    return v;
}
static uint64_t load64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8u * i);
    return v;
}

int nova_uefi_normalize_map(const void *descriptors, size_t bytes,
                            size_t stride, struct nova_uefi_region *out,
                            size_t capacity, size_t *out_count) {
    if (!out_count) return -1;
    *out_count = 0;
    if (!descriptors || !out || bytes == 0 ||
        stride < NOVA_UEFI_DESCRIPTOR_MIN_SIZE ||
        stride > bytes || bytes % stride != 0 ||
        capacity == 0 || capacity > NOVA_UEFI_MAX_REGIONS)
        return -1;
    const size_t count = bytes / stride;
    if (count > capacity) return -1;
    const uint8_t *data = (const uint8_t *)descriptors;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *d = data + i * stride;
        const uint32_t kind = load32(d);
        const uint64_t base = load64(d + 8u);
        const uint64_t pages = load64(d + 24u);
        if (!pages || base % NOVA_UEFI_PAGE_SIZE != 0 ||
            pages > UINT64_MAX / NOVA_UEFI_PAGE_SIZE)
            return -1;
        const uint64_t length = pages * NOVA_UEFI_PAGE_SIZE;
        if (base > UINT64_MAX - length) return -1;
        /* Reject overlapping descriptors: prevent falsely usable RAM. */
        for (size_t j = 0; j < i; ++j) {
            const uint64_t prev_end = out[j].base + out[j].length;
            if (base < prev_end && out[j].base < base + length)
                return -1;
        }
        out[i].base = base;
        out[i].length = length;
        out[i].type = kind == NOVA_UEFI_CONVENTIONAL_MEMORY ? 1u : 2u;
        out[i].reserved = 0u;
    }
    *out_count = count;
    return 0;
}
