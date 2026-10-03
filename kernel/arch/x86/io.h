/* NOVA OS arch_io backend: x86 (Phase 1).
 * Generic kernel code must use these, never raw in/out elsewhere.
 * x86_64 backend is identical hardware; kept as a separate file so
 * future divergence (e.g. different port maps) has a home.
 */
#ifndef NOVA_ARCH_X86_IO_H
#define NOVA_ARCH_X86_IO_H

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

static inline void arch_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}

#endif /* NOVA_ARCH_X86_IO_H */
