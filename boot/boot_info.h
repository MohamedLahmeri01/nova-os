/* NOVA OS boot information structure (Phase 0 prototype).
 * Firmware-decoupled: the bootloader fills this; generic kernel code
 * only reads this struct, never firmware tables directly.
 * Status: header-only contract, untested against real firmware.
 */
#ifndef NOVA_BOOT_INFO_H
#define NOVA_BOOT_INFO_H

#include <stdint.h>

#define NOVA_BOOT_INFO_VERSION 1u
#define NOVA_BOOT_MAGIC 0x4E564F53424F4F54ull /* "NOVOSBOOT" */

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type; /* 1 = usable RAM, others reserved */
    uint32_t reserved;
} nova_mem_region_t;

typedef struct {
    uint64_t magic;              /* must equal NOVA_BOOT_MAGIC */
    uint32_t version;            /* must equal NOVA_BOOT_INFO_VERSION */
    uint32_t flags;
    const char *cmdline;         /* bootloader-provided, may be NULL */
    nova_mem_region_t *regions;  /* normalized memory map */
    uint32_t region_count;
    uint64_t fb_base;            /* framebuffer, 0 if absent */
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
} nova_boot_info_t;

#endif /* NOVA_BOOT_INFO_H */
