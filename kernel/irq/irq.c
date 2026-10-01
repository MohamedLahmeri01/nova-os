/* NOVA OS interrupts: IDT, PIC, trap/IRQ handlers (Phase 3).
 * 48 IDT entries (0-31 exceptions -> trap_common, 32-47 IRQs ->
 * irq_common), all DPL0 interrupt gates. Exceptions are fatal by design
 * until Phase 4+ installs real fault handlers. PIC is remapped to
 * 0x20/0x28 with only IRQ0 (PIT) unmasked; spurious 7/15 handled per
 * spec. LAPIC probe lives in lapic.c.
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "irq.h"
#include "time/time.h"
#include "sched/sched.h"
#include "time.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "irq: no arch backend for this architecture"
#endif

#define PIC_M_CMD 0x20u
#define PIC_M_DATA 0x21u
#define PIC_S_CMD 0xA0u
#define PIC_S_DATA 0xA1u
#define PIC_EOI 0x20u

typedef struct {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t zero;
    uint8_t attr;
    uint16_t off_hi;
} __attribute__((packed)) idt_ent_t;

static idt_ent_t g_idt[256];

extern void *irq_stub_table[48];

static const char *const trap_names[32] = {
    "DE", "DB", "NMI", "BP", "OF", "BR", "UD", "NM",
    "DF", "CSO", "TS", "NP", "SS", "GP", "PF", "RSV",
    "MF", "AC", "MC", "XM", "VE", "CP", "HV", "VC",
    "SX", "RSV", "RSV", "RSV", "RSV", "RSV", "RSV", "RSV"
};

static void idt_set(int n, void *fn) {
    uintptr_t a = (uintptr_t)fn;
    g_idt[n].off_lo = (uint16_t)(a & 0xFFFFu);
    g_idt[n].sel = 0x08u;
    g_idt[n].zero = 0;
    g_idt[n].attr = 0x8Eu; /* present, DPL0, 32-bit interrupt gate */
    g_idt[n].off_hi = (uint16_t)((a >> 16) & 0xFFFFu);
}

static void pic_remap(void) {
    arch_outb(PIC_M_CMD, 0x11);
    arch_outb(PIC_S_CMD, 0x11);
    arch_outb(PIC_M_DATA, 0x20);
    arch_outb(PIC_S_DATA, 0x28);
    arch_outb(PIC_M_DATA, 0x04);
    arch_outb(PIC_S_DATA, 0x02);
    arch_outb(PIC_M_DATA, 0x01);
    arch_outb(PIC_S_DATA, 0x01);
    /* Unmask IRQ0 (timer) only: 0xFE, NOT 0xFD (bit 0 = IRQ0; 0xFD
     * masks the timer and opens the keyboard - a silent no-ticks
     * hang that mask readback cannot catch if it checks the same
     * wrong constant). */
    arch_outb(PIC_M_DATA, 0xFEu);
    arch_outb(PIC_S_DATA, 0xFFu);
    /* IMCR: force PIC mode so the 8259 INT reaches the CPU directly. */
    arch_outb(0x22u, 0x70u);
    arch_outb(0x23u, 0x00u);
}

void trap_handler(trap_frame_t *f) {
    const char *name = (f->vec < 32) ? trap_names[f->vec] : "IRQ";
    serial_puts("TRAP vec=");
    serial_putdec32(f->vec);
    serial_puts(" (");
    serial_puts(name);
    serial_puts(") err=0x");
    serial_puthex32(f->err);
    serial_puts(" eip=0x");
    serial_puthex32(f->eip);
    serial_puts(" cs=0x");
    serial_puthex32(f->cs);
    serial_puts(" eflags=0x");
    serial_puthex32(f->eflags);
    serial_putc('\n');
    if (f->vec == 14) {
        serial_puts(" pf-addr=0x");
        serial_puthex32(arch_read_cr2());
        serial_putc('\n');
    }
    {
        static char reason[16];
        uint32_t i = 0;
        reason[i++] = 't';
        reason[i++] = 'r';
        reason[i++] = 'a';
        reason[i++] = 'p';
        reason[i++] = '-';
        for (const char *s = name; *s && i < 15; s++) {
            reason[i++] = *s;
        }
        reason[i] = 0;
        nova_panic(reason);
    }
}

void irq_handler(trap_frame_t *f) {
    g_irq_count++;
    g_last_vec = f->vec;
    if (f->vec == 32) {
        g_ticks++;
        sched_tick();
    }
    if (f->vec >= 40) {
        arch_outb(PIC_S_CMD, PIC_EOI);
    }
    arch_outb(PIC_M_CMD, PIC_EOI);
}

uint32_t g_irq_count;
uint32_t g_last_vec;

void irq_init(void) {
    for (int i = 0; i < 48; i++) {
        idt_set(i, irq_stub_table[i]);
    }
    arch_load_idt(g_idt, (uint16_t)(sizeof(g_idt) - 1u));
    {
        /* NOTE: must use packed arch_idtr_t (6 bytes); a padded local
         * struct misreads the base (learned the hard way). */
        arch_idtr_t idtr;
        arch_store_idt(&idtr);
        if (idtr.limit != sizeof(g_idt) - 1u ||
            idtr.base != (uint32_t)g_idt) {
            nova_panic("idt-load-fail");
        }
    }
    pic_remap();
    if (arch_inb(PIC_M_DATA) != 0xFEu || arch_inb(PIC_S_DATA) != 0xFFu) {
        nova_panic("pic-mask-fail");
    }
    lapic_probe();
    serial_puts("IDT-OK\n");
}
