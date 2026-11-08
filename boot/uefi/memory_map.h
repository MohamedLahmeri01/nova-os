/* NOVA OS P3: UEFI memory-map normalization, freestanding and host-testable.
 * This is NOT a UEFI entrypoint or a bootable x86-64 loader.
 */
#ifndef NOVA_UEFI_MEMORY_MAP_H
#define NOVA_UEFI_MEMORY_MAP_H
#include <stdint.h>
#include <stddef.h>

#define NOVA_UEFI_PAGE_SIZE UINT64_C(4096)
#define NOVA_UEFI_CONVENTIONAL_MEMORY 7u
#define NOVA_UEFI_DESCRIPTOR_MIN_SIZE 40u
#define NOVA_UEFI_MAX_REGIONS 256u

/* Fixed-width, firmware-independent handoff records. */
struct nova_uefi_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;  /* 1 usable, 2 reserved */
    uint32_t reserved;
};

/* Caller owns both buffers and must call after firmware's GetMemoryMap.
 * Only conventional memory is usable; boot services/loader memory stays
 * reserved until explicit lifecycle management exists. Runtime memory,
 * MMIO, and unrecognized types are always reserved.
 * Descriptor layout: EFI_MEMORY_DESCRIPTOR v1, stride from firmware.
 * Returns 0 on success; -1 on malformed input or capacity failure.
 * On failure, *out_count is 0; output buffer contents are undefined.
 * No firmware pointers or untrusted ranges are dereferenced.
 */
int nova_uefi_normalize_map(const void *descriptors, size_t bytes,
                            size_t stride, struct nova_uefi_region *out,
                            size_t capacity, size_t *out_count);
#endif
