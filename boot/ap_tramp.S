/* NOVA OS AP trampoline (Phase 4c).
 * Linked into kernel .text; BSP copies [_ap_tramp_start, _ap_tramp_end)
 * to AP_TRAMP_BASE and SIPIs the vector. The 16-bit part uses ONLY
 * numeric literals (see ap_addrs.h): absolute link-VMA relocations
 * would truncate above 64K in 16-bit addressing. Post-far-jump 32-bit
 * code uses normal symbols. Per-AP values (stack, LAPIC base) live in
 * _ap_* slots (32-bit use only).
 */
#include "ap_addrs.h"

.code16
.text
.globl _ap_tramp_start
_ap_tramp_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    lgdt AP_SCR_GDTR
    movl AP_SCR_CR3, %eax
    movl %eax, %cr3
    movl AP_SCR_CR4, %eax
    movl %eax, %cr4
    movl AP_SCR_CR0, %eax
    movl %eax, %cr0
    pushw $0x08
    pushw AP_SCR_IP
    lret
.code32
.globl _ap_prot_addr
_ap_prot_addr:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl _ap_stack, %esp
    movw $0x3F8, %dx
    movb $67, %al
    outb %al, %dx
    movl _ap_lapic, %eax
    movl 0x20(%eax), %eax
    shrl $24, %eax
    pushl %eax
    /* Absolute indirect call: a relative call would land at
     * link_target + (0x8000 - link_base) (garbage); the target stays
     * in kernel text. 32-bit absolute fits (16-bit would truncate). */
    movl $_ap_entry, %eax
    call *%eax
    addl $4, %esp
1:
    cli
    hlt
    jmp 1b
.globl _ap_tramp_end
_ap_tramp_end:
.globl _ap_stack
_ap_stack:
    .long 0
.globl _ap_lapic
_ap_lapic:
    .long 0
