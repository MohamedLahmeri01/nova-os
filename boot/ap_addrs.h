/* NOVA OS AP boot addresses (Phase 4c, shared contract).
 * The AP trampoline runs at AP_TRAMP_BASE in real mode, where only
 * 16-bit displacements work. To avoid link-VMA relocations (which
 * truncate above 64K), the 16-bit part touches ONLY numeric literals
 * below: BSP fills the scratch area, trampoline reads it. Post-far-jump
 * 32-bit code uses normal symbols (32-bit relocations are fine).
 * Included from both ap_tramp.S (.S runs through cpp) and smp.c.
 */
#ifndef NOVA_AP_ADDRS_H
#define NOVA_AP_ADDRS_H

#define AP_TRAMP_BASE 0x8000
#define AP_TRAMP_VECTOR 0x08

/* Real-mode scratch (stage1 stack area, boot long done). */
#define AP_SCR_GDTR 0x7000 /* 6 bytes: limit(u16), base(u32) */
#define AP_SCR_CR3 0x7006
#define AP_SCR_CR4 0x700A
#define AP_SCR_CR0 0x700E
#define AP_SCR_IP 0x7012 /* word: protected entry offset from base */

#endif /* NOVA_AP_ADDRS_H */
