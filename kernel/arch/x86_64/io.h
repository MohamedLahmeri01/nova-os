/* NOVA OS arch_io backend: x86_64 (Phase 1).
 * Identical to x86 today (same port hardware); separate file by design,
 * see kernel/arch/x86/io.h. Generic code includes one of these based on
 * __i386__ / __x86_64__, never inline asm directly.
 */
#ifndef NOVA_ARCH_X86_64_IO_H
#define NOVA_ARCH_X86_64_IO_H

#include <stdint.h>

static inline void arch_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t arch_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline uint16_t arch_inw(uint16_t port) {
    uint16_t r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

#endif /* NOVA_ARCH_X86_64_IO_H */
