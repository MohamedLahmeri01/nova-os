/* NOVA OS GDT/TSS interface (Phase 4b, 32-bit x86).
 * gdt_init builds the kernel GDT (null, kernel code/data, user
 * code/data, TSS) and loads it; the running flat segments are
 * unaffected (hidden descriptor parts persist). ltr and TSS.ESP0
 * setup happen in the user test (IDT active there, so faults print).
 */
#ifndef NOVA_GDT_H
#define NOVA_GDT_H

#include <stdint.h>

#define GDT_KCODE 0x08u
#define GDT_KDATA 0x10u
#define GDT_UCODE 0x1Bu
#define GDT_UDATA 0x23u
#define GDT_TSS 0x28u

void gdt_init(void);
void tss_set_esp0(uint32_t esp);
void tss_load(void); /* ltr; call once IDT is active (faults print) */

#endif /* NOVA_GDT_H */
