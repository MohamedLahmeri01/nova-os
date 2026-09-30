/* NOVA OS LAPIC probe (Phase 3).
 * Detection only: CPUID APIC bit + IA32_APIC_BASE MSR (base, BSP,
 * enabled). The 8259 stays in charge; APIC enable + SMP bring-up
 * belong to Phase 4+. MMIO version read needs mapped MMIO (Phase 8).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "irq.h"
#include "mm/vmm.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "lapic: no arch_cpu backend for this architecture"
#endif

#define IA32_APIC_BASE 0x1Bu

void lapic_probe(void) {
    uint32_t a, b, c, d;
    arch_cpuid(1u, &a, &b, &c, &d);
    if ((d & (1u << 9)) == 0) {
        nova_panic("no-apic");
    }
    uint64_t msr = arch_read_msr(IA32_APIC_BASE);
    if ((msr & (1ULL << 11)) == 0) {
        nova_panic("apic-disabled");
    }
    serial_puts("LAPIC base=0x");
    serial_puthex32((uint32_t)(msr & ~0xFFFULL));
    serial_puts(" bsp=");
    serial_putdec32((uint32_t)((msr >> 8) & 1u));
    serial_puts(" enabled=1\n");
    /* Virtual-wire mode: route the 8259 INT through LINT0 as ExtINT
     * (and LINT1 as NMI). Without this, PIC IRQs latch in IRR but the
     * CPU never sees them on firmware that leaves LINT masked. */
    vmm_map_mmio(0xFEE00000u);
    {
        volatile uint32_t *lapic = (volatile uint32_t *)0xFEE00000u;
        /* Virtual-wire mode: 8259 INT through LINT0 as ExtINT, LINT1
         * as NMI. TPR to zero (else it masks PIC vectors), SIVR
         * software-enable (else the LAPIC ignores LVT entries). */
        lapic[0x350u / 4u] = 0x700u;
        lapic[0x360u / 4u] = 0x400u;
        lapic[0x80u / 4u] = 0;
        lapic[0xF0u / 4u] = 0x1FFu;
        if (lapic[0x350u / 4u] != 0x700u) {
            nova_panic("lint0-write-fail");
        }
    }
}
