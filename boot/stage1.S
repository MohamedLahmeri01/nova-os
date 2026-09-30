/* NOVA OS stage1: BIOS boot sector (Phase 1 prototype).
 *
 * Assembled with mingw GAS (.code16), linked at 0x7C00 via boot/stage1.ld,
 * objcopy -O binary -j .text. Must fit in 510 bytes (build asserts;
 * signature appended by the image builder, not here).
 *
 * Size discipline: constant structures (DAP) live as patched templates;
 * nova_boot_info is filled with stosl, not 10-byte absolute stores.
 *
 * Job: load stage2 (kernel) at linear 0x10000 via INT13 extensions, collect
 * E820 map, fill nova_boot_info at 0x500 (regions at 0x5000, 24B each),
 * enable A20 (BIOS then port 0x92, verified), enter 32-bit protected mode
 * (flat GDT), jump to 0x10000.
 *
 * Memory use: 0x500 boot_info, 0x600+ scratch, 0x5F0 E820 count temp,
 * 0x5000 regions, stack 0x7000, pm stack 0x90000, kernel 0x10000.
 * Errors print one letter via BIOS teletype, then halt. D=disk, A=A20.
 */
.code16
.globl _start
.text
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7000, %sp
    sti
    cld
    movb %dl, boot_drive

    /* INT13 extensions present? */
    movb $0x41, %ah
    movw $0x55AA, %bx
    movb boot_drive, %dl
    int $0x13
    jc disk_err
    cmpw $0xAA55, %bx
    jne disk_err

    /* Patch DAP count (image builder filled ksec_patch), then load. */
    movw ksec_patch, %ax
    testw %ax, %ax
    jz disk_err
    movw %ax, dap_count
    movw $dap, %si
    movb $0x42, %ah
    movb boot_drive, %dl
    int $0x13
    jc disk_err

    /* A20: BIOS call first, then fast gate at port 0x92. */
    movw $0x2401, %ax
    int $0x15
    inb $0x92, %al
    testb $2, %al
    jnz a20_ok
    orb $2, %al
    andb $0xFE, %al
    outb %al, $0x92
a20_ok:
    /* Verify: 0x500 must NOT alias 0x100500. */
    movw $0x500, %si
    movw (%si), %ax
    pushw %ax
    movw $0x1234, (%si)
    movw $0xFFFF, %cx
    movw %cx, %es
    movw $0x0510, %di
    movw %es:(%di), %cx
    popw %ax
    movw %ax, (%si)
    xorw %ax, %ax
    movw %ax, %es
    cmpw $0x1234, %cx
    je a20_err

    /* E820 map into 0x5000 with 24-byte stride, matching nova_mem_region_t
     * directly (no conversion step). Request 24 bytes; the attrs/reserved
     * dword is zeroed so the contract holds whether firmware writes 20
     * or 24 bytes. Cap 32 entries (ends at 0x5300). */
    xorl %ebx, %ebx
    movw $0x5000, %di
    movb $0, 0x5F0
e820_loop:
    movl $0xE820, %eax
    movl $0x534D4150, %edx
    movl $24, %ecx
    int $0x15
    jc e820_done
    cmpl $0x534D4150, %eax
    jne e820_fallback
    movl $0, %es:20(%di)
    addw $24, %di
    incb 0x5F0
    testl %ebx, %ebx
    jz e820_done
    cmpb $32, 0x5F0
    jae e820_done
    jmp e820_loop
e820_done:
    cmpb $0, 0x5F0
    jne e820_have
e820_fallback:
    /* Two plausible regions, 24-byte stride. Rarely used. */
    movw $0x5000, %di
    xorl %eax, %eax
    stosl
    stosl
    movl $0x9FC00, %eax
    stosl
    xorl %eax, %eax
    stosl
    movl $1, %eax
    stosl
    xorl %eax, %eax
    stosl
    movl $0x100000, %eax
    stosl
    xorl %eax, %eax
    stosl
    movl $0x3FF00000, %eax
    stosl
    xorl %eax, %eax
    stosl
    movl $1, %eax
    stosl
    xorl %eax, %eax
    stosl
    movb $2, 0x5F0
e820_have:

    /* Fill nova_boot_info at 0x500 (12 dwords, see boot/boot_info.h). */
    movw $0x500, %di
    movl $0x424F4F54, %eax
    stosl
    movl $0x4E564F53, %eax
    stosl
    movl $1, %eax
    stosl
    xorl %eax, %eax
    stosl
    stosl
    movl $0x5000, %eax
    stosl
    movb 0x5F0, %al
    movzbl %al, %eax
    stosl
    xorl %eax, %eax
    stosl
    stosl
    stosl
    stosl
    stosl

    /* Protected mode, flat 4GB segments. */
    cli
    lgdt gdtr
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $pm32
.code32
pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    movl $0x10000, %eax
    jmp *%eax

/* Error paths: print letter, halt. */
disk_err:
    movb $68, %al          /* 'D' */
    jmp bios_halt_putc
a20_err:
    movb $65, %al          /* 'A' */
bios_halt_putc:
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
1:
    cli
    hlt
    jmp 1b

boot_drive:
    .byte 0
dap:
    .byte 16, 0
dap_count:
    .word 8                /* patched at runtime from ksec_patch */
    .word 0                /* offset */
    .word 0x1000           /* segment -> linear 0x10000 */
    .long 1, 0             /* LBA */
nvsc_magic:
    .long 0x4353564E       /* file bytes "NVSC"; count word follows */
ksec_patch:
    .word 8                /* overwritten by image builder */
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdtr:
    .word 23
    .long gdt
