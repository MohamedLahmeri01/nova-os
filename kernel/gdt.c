/* NOVA OS GDT + TSS (Phase 4b, 32-bit x86 only).
 * 8 entries: null, kernel code/data (same as stage1's), user code/data
 * (DPL3), 32-bit available TSS, spare. lgdt is safe mid-flight (flat
 * segments unchanged); ltr runs later under IDT cover (user.c).
 */
#include <stdint.h>

#include "gdt.h"

static uint64_t g_gdt[8];
static uint32_t g_tss[26];

static uint64_t desc(uint32_t base, uint32_t limit, uint8_t access,
                     uint8_t flags) {
    return ((uint64_t)(limit & 0xFFFFu)) |
           (((uint64_t)(base & 0xFFFFFFu)) << 16) |
           (((uint64_t)access) << 40) |
           (((uint64_t)(limit & 0xF0000u)) << 32) |
           (((uint64_t)(flags & 0xFu)) << 52) |
           (((uint64_t)(base & 0xFF000000u)) << 32);
}

void gdt_init(void) {
    g_gdt[0] = 0;
    g_gdt[1] = desc(0, 0xFFFFF, 0x9A, 0xC); /* kernel code */
    g_gdt[2] = desc(0, 0xFFFFF, 0x92, 0xC); /* kernel data */
    g_gdt[3] = desc(0, 0xFFFFF, 0xFA, 0xC); /* user code (DPL3) */
    g_gdt[4] = desc(0, 0xFFFFF, 0xF2, 0xC); /* user data (DPL3) */
    g_gdt[5] = desc((uint32_t)&g_tss, 103, 0x89, 0x0); /* TSS */
    g_gdt[6] = 0;
    g_gdt[7] = 0;
    for (int i = 0; i < 26; i++) {
        g_tss[i] = 0;
    }
    g_tss[2] = 0x10u; /* ss0 = kernel data */
    g_tss[25] = 104u << 16; /* iomap base past the end: no bitmap */
    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) gdtr;
        gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1u);
        gdtr.base = (uint32_t)&g_gdt;
        __asm__ volatile("lgdt %0" :: "m"(gdtr) : "memory");
    }
}

void tss_set_esp0(uint32_t esp) {
    g_tss[1] = esp;
}

void tss_load(void) {
    __asm__ volatile("ltrw %0" :: "r"((uint16_t)GDT_TSS) : "memory");
}
