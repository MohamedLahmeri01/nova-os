/* NOVA OS stage2 entry trampoline (Phase 2b).
 * Linked first at 0x10000 via boot/stage2.ld (.text.boot kept first).
 * stage1 jumps here in 32-bit protected mode, flat segments, ESP set.
 * Zeroes .bss (NOLOAD: absent from the image), normalizes EFLAGS,
 * calls nova_pm_main(boot_info at NOVA_BOOT_INFO_AT); never returns.
 */
#include "mem_layout.h"

.code32
.globl _stage2_start
.section .text.boot, "ax"
_stage2_start:
    /* Zero EBP so the panic handler's frame walk terminates cleanly,
     * and normalize EFLAGS (deterministic, IF clear). */
    xorl %ebp, %ebp
    pushl $0x2
    popfl
    cld
    /* Zero .bss: rep stosb needs no alignment assumptions. */
    movl $bss_start, %edi
    movl $kernel_end, %ecx
    subl %edi, %ecx
    jz 2f
    xorl %eax, %eax
    rep stosb
2:
    pushl $NOVA_BOOT_INFO_AT
    call _nova_pm_main
    addl $4, %esp
1:
    cli
    hlt
    jmp 1b
