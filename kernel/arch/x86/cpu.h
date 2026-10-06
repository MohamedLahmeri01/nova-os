/* NOVA OS arch_cpu backend: x86 (Phase 2b).
 * Privileged CPU control (CR/MSR/CPUID) lives here and only here.
 * Generic code (mm/vmm) uses these; never raw mov-cr/rdmsr elsewhere.
 */
#ifndef NOVA_ARCH_X86_CPU_H
#define NOVA_ARCH_X86_CPU_H

#include <stdint.h>

static inline void arch_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                              uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf)
                     : "memory");
}

static inline uint32_t arch_read_cr0(void) {
    uint32_t v;
    __asm__ volatile("movl %%cr0, %0" : "=r"(v) :: "memory");
    return v;
}

static inline void arch_write_cr0(uint32_t v) {
    __asm__ volatile("movl %0, %%cr0" :: "r"(v) : "memory");
}

static inline uint32_t arch_read_cr2(void) {
    uint32_t v;
    __asm__ volatile("movl %%cr2, %0" : "=r"(v) :: "memory");
    return v;
}

static inline uint32_t arch_read_cr3(void) {
    uint32_t v;
    __asm__ volatile("movl %%cr3, %0" : "=r"(v) :: "memory");
    return v;
}

static inline void arch_write_cr3(uint32_t v) {
    __asm__ volatile("movl %0, %%cr3" :: "r"(v) : "memory");
}

static inline uint32_t arch_read_cr4(void) {
    uint32_t v;
    __asm__ volatile("movl %%cr4, %0" : "=r"(v) :: "memory");
    return v;
}

static inline void arch_write_cr4(uint32_t v) {
    __asm__ volatile("movl %0, %%cr4" :: "r"(v) : "memory");
}

static inline uint64_t arch_read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void arch_write_msr(uint32_t msr, uint64_t v) {
    __asm__ volatile("wrmsr" :: "c"(msr),
                     "a"((uint32_t)v), "d"((uint32_t)(v >> 32))
                     : "memory");
}

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) arch_idtr_t;

static inline void arch_load_idt(const void *base, uint16_t limit) {
    arch_idtr_t idtr;
    idtr.limit = limit;
    idtr.base = (uint32_t)base;
    __asm__ volatile("lidt %0" :: "m"(idtr) : "memory");
}

static inline void arch_store_idt(void *out) {
    __asm__ volatile("sidt %0" : "=m"(*(arch_idtr_t *)out) :: "memory");
}

static inline void arch_store_gdt(void *out) {
    __asm__ volatile("sgdt %0" : "=m"(*(arch_idtr_t *)out) :: "memory");
}

static inline void arch_cli(void) {
    __asm__ volatile("cli" ::: "memory");
}

static inline void arch_sti(void) {
    __asm__ volatile("sti" ::: "memory");
}

/* IF save/restore for irqsave locks (the restore end must know
 * whether interrupts were on: blind sti inside an ISR handler
 * re-enables mid-handler — see ADR-0061). */
static inline uint32_t arch_irq_save(void) {
    uint32_t eflags;
    __asm__ volatile("pushfl; popl %0" : "=r"(eflags)::"memory");
    __asm__ volatile("cli" ::: "memory");
    return eflags & 0x200u;
}

static inline void arch_irq_restore(uint32_t was_on) {
    if (was_on != 0) {
        __asm__ volatile("sti" ::: "memory");
    }
}

static inline void arch_hlt(void) {
    __asm__ volatile("hlt" ::: "memory");
}

static inline void arch_invlpg(uint32_t addr) {
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

/* SMP atomics (Phase 0.3-E): LOCK-prefixed, bus-locking on 486+,
 * cache-locking on modern cores. Generic code uses these, never
 * inline LOCK asm directly. Correct on one CPU too (atomicity
 * vs interrupts comes from the caller's cli discipline, not
 * from LOCK). */
static inline void arch_pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

/* Atomic exchange: stores v, returns the previous value. */
static inline uint32_t arch_atomic_xchg_u32(volatile uint32_t *p,
                                            uint32_t v) {
    __asm__ volatile("xchgl %0, %1"
                     : "+r"(v), "+m"(*p)::"memory");
    return v;
}

/* Atomic compare-exchange: if *p == expect, stores want.
 * Returns the previous value (compare with expect to tell). */
static inline uint32_t arch_atomic_cmpxchg_u32(volatile uint32_t *p,
                                               uint32_t expect,
                                               uint32_t want) {
    uint32_t prev = expect;
    __asm__ volatile("lock; cmpxchgl %2, %1"
                     : "+a"(prev), "+m"(*p)
                     : "r"(want)
                     : "memory");
    return prev;
}

/* Atomic fetch-and-add: adds v, returns the previous value. */
static inline uint32_t arch_atomic_add_u32(volatile uint32_t *p,
                                           uint32_t v) {
    __asm__ volatile("lock; xaddl %0, %1"
                     : "+r"(v), "+m"(*p)::"memory");
    return v;
}

#endif /* NOVA_ARCH_X86_CPU_H */
