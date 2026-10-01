/* NOVA OS SMP bring-up (Phase 4c).
 * MP-table CPU enumeration (BIOS ROM + EBDA scan, checksummed), then
 * sequential AP start: copy position-independent trampoline to 0x8000,
 * INIT/SIPI via LAPIC ICR, tick-bounded wait per AP. APs record their
 * LAPIC ID, print, and park (cli + hlt; no AP scheduling yet — single
 * runqueue on BSP, balancing deferred). AP stacks leak by design here
 * (parked forever); PMM cost is 8KB per AP, documented.
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "smp.h"
#include "time/time.h"
#include "../../boot/ap_addrs.h"

#if defined(__i386__)
#include "arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "arch/x86_64/cpu.h"
#else
#error "smp: no arch_cpu backend for this architecture"
#endif

#define MAX_APS 8u
#define AP_TRAMP_BASE 0x8000u
#define AP_TRAMP_VECTOR 0x08u
#define IA32_APIC_BASE 0x1Bu
#define LAPIC_ICR_LO 0x300u
#define LAPIC_ICR_HI 0x310u

extern uint8_t ap_tramp_start[];
extern uint8_t ap_tramp_end[];
extern uint32_t ap_stack;
extern uint32_t ap_lapic;
extern void ap_prot_addr(void);

static uint32_t g_apic_ids[MAX_APS];
static uint32_t g_ap_count;
static uint32_t g_ncpus;
static volatile uint32_t g_ap_done;
static uint32_t g_ap_online;
static uint32_t g_lapic_base;

static int mem_eq(const void *a, const void *b, uint32_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return 0;
        }
    }
    return 1;
}

static const uint8_t *mp_scan(const uint8_t *base, uint32_t len) {
    static const char sig[4] = { '_', 'M', 'P', '_' };
    for (uint32_t off = 0; off + 16u <= len; off += 16u) {
        const uint8_t *p = base + off;
        uint32_t sum = 0;
        if (!mem_eq(p, sig, 4)) {
            continue;
        }
        for (uint32_t i = 0; i < 16u; i++) {
            sum += p[i];
        }
        if ((sum & 0xFFu) == 0) {
            return p;
        }
    }
    return 0;
}

static void mp_parse(void) {
    const uint8_t *fp;
    const uint8_t *mpct;
    uint32_t total_len;
    uint32_t off;
    g_ncpus = 0;
    g_ap_count = 0;
    fp = mp_scan((const uint8_t *)0xF0000u, 0x10000u);
    if (fp == 0) {
        uint32_t ebda = ((uint32_t)(*(volatile uint16_t *)0x40Eu)) << 4;
        if (ebda != 0 && ebda < 0x100000u) {
            fp = mp_scan((const uint8_t *)ebda, 1024u);
        }
    }
    if (fp == 0) {
        serial_puts("SMP-1CPU (no MP table)\n");
        g_ncpus = 1;
        return;
    }
    mpct = (const uint8_t *)(fp[4] | ((uint32_t)fp[5] << 8) |
                             ((uint32_t)fp[6] << 16) |
                             ((uint32_t)fp[7] << 24));
    if (!mem_eq(mpct, "PCMP", 4)) {
        nova_panic("mp-bad-header");
    }
    total_len = mpct[4] | ((uint32_t)mpct[5] << 8);
    off = 44;
    while (off < total_len) {
        uint8_t type = mpct[off];
        if (type == 0) {
            /* CPU entry: [1]=APIC ID, [2]=APIC version, [3]=flags
             * (bit0 usable, bit1 BSP). NOT [2] (a classic misread:
             * the version byte looks constant across CPUs). */
            uint8_t apic_id = mpct[off + 1];
            uint8_t flags = mpct[off + 3];
            if (flags & 1u) {
                if (g_ncpus < MAX_APS + 1u) {
                    g_ncpus++;
                }
                if (!(flags & 2u) && g_ap_count < MAX_APS) {
                    g_apic_ids[g_ap_count++] = apic_id;
                }
            }
            off += 20;
        } else if (type == 1 || type == 2 || type == 3 || type == 4) {
            off += 8;
        } else {
            nova_panic("mp-bad-type");
        }
    }
}

static uint32_t lapic_base(void) {
    if (g_lapic_base == 0) {
        g_lapic_base =
            (uint32_t)(arch_read_msr(IA32_APIC_BASE) & ~0xFFFULL);
    }
    return g_lapic_base;
}

static void lapic_ipi(uint32_t id, uint32_t lo) {
    volatile uint32_t *lapic = (volatile uint32_t *)lapic_base();
    lapic[LAPIC_ICR_HI / 4u] = id << 24;
    lapic[LAPIC_ICR_LO / 4u] = lo;
}

static void tick_wait(uint32_t n) {
    uint32_t t0 = g_ticks;
    while (g_ticks - t0 < n) {
        __asm__ volatile("pause");
    }
}

void ap_entry(uint32_t apic_id) {
    serial_puts("AP ");
    serial_putdec32(apic_id);
    serial_puts(" online\n");
    g_ap_online++;
    g_ap_done = 1;
    __asm__ volatile("cli" ::: "memory");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void smp_boot(void) {
    uint32_t tramp_len;
    mp_parse();
    serial_puts("SMP cpus=");
    serial_putdec32(g_ncpus);
    serial_putc('\n');
    if (g_ap_count == 0) {
        serial_puts("SMP-OK\n");
        return;
    }
    tramp_len = (uint32_t)(ap_tramp_end - ap_tramp_start);
    if (tramp_len == 0 || tramp_len > 4096u) {
        nova_panic("smp-bad-tramp");
    }
    for (uint32_t i = 0; i < g_ap_count; i++) {
        uint32_t stack = pmm_alloc_contig(2);
        uint8_t *dst = (uint8_t *)AP_TRAMP_BASE;
        if (stack == 0) {
            nova_panic("smp-no-stack");
        }
        /* Fill real-mode scratch (literals only on the AP side),
         * then copy the filled image to AP_TRAMP_BASE. Per-AP values
         * that post-jump 32-bit code needs stay as link symbols. */
        {
            arch_idtr_t gdtr;
            arch_store_gdt(&gdtr);
            *(uint16_t *)AP_SCR_GDTR = gdtr.limit;
            *(uint32_t *)(AP_SCR_GDTR + 2u) = gdtr.base;
        }
        *(uint32_t *)AP_SCR_CR3 = arch_read_cr3();
        *(uint32_t *)AP_SCR_CR4 = arch_read_cr4();
        *(uint32_t *)AP_SCR_CR0 = arch_read_cr0();
        *(uint16_t *)AP_SCR_IP =
            (uint16_t)(AP_TRAMP_BASE + ((uint32_t)&ap_prot_addr -
                                        (uint32_t)&ap_tramp_start));
        ap_stack = stack + 8192u;
        ap_lapic = lapic_base();
        for (uint32_t k = 0; k < tramp_len; k++) {
            dst[k] = ap_tramp_start[k];
        }
        g_ap_done = 0;
        lapic_ipi(g_apic_ids[i], 0x4500u);
        tick_wait(1);
        lapic_ipi(g_apic_ids[i], 0x4400u);
        tick_wait(1);
        lapic_ipi(g_apic_ids[i], 0x4600u | AP_TRAMP_VECTOR);
        {
            uint32_t spin = 0;
            while (spin++ < 200000u) {
                __asm__ volatile("pause");
            }
        }
        lapic_ipi(g_apic_ids[i], 0x4600u | AP_TRAMP_VECTOR);
        {
            uint32_t t0 = g_ticks;
            while (!g_ap_done) {
                if (g_ticks - t0 > 200u) {
                    nova_panic("ap-start-timeout");
                }
                __asm__ volatile("pause");
            }
        }
    }
    if (g_ap_online != g_ap_count) {
        nova_panic("smp-count-mismatch");
    }
    serial_puts("SMP-OK\n");
}

uint32_t smp_cpu_count(void) {
    return g_ncpus;
}
