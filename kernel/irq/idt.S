/* NOVA OS IDT stubs (Phase 3).
 * 48 entry points (vectors 0-47): exceptions push a dummy error code
 * where the CPU pushes none, then every stub pushes its vector number
 * and jumps to a common handler. trap_common builds the C frame and
 * calls _trap_handler (noreturn); irq_common calls _irq_handler,
 * tears down, and irets. Plain GAS, no includes, assembled -m32.
 */
.code32
.text

.macro VEC n, err, common
.globl irq_stub_\n
irq_stub_\n:
    .if \err == 0
    pushl $0
    .endif
    pushl $\n
    jmp \common
.endm

VEC 0,0,trap_common
VEC 1,0,trap_common
VEC 2,0,trap_common
VEC 3,0,trap_common
VEC 4,0,trap_common
VEC 5,0,trap_common
VEC 6,0,trap_common
VEC 7,0,trap_common
VEC 8,1,trap_common
VEC 9,0,trap_common
VEC 10,1,trap_common
VEC 11,1,trap_common
VEC 12,1,trap_common
VEC 13,1,trap_common
VEC 14,1,trap_common
VEC 15,0,trap_common
VEC 16,0,trap_common
VEC 17,1,trap_common
VEC 18,0,trap_common
VEC 19,0,trap_common
VEC 20,0,trap_common
VEC 21,1,trap_common
VEC 22,0,trap_common
VEC 23,0,trap_common
VEC 24,0,trap_common
VEC 25,0,trap_common
VEC 26,0,trap_common
VEC 27,0,trap_common
VEC 28,0,trap_common
VEC 29,1,trap_common
VEC 30,1,trap_common
VEC 31,0,trap_common
VEC 32,0,irq_common
VEC 33,0,irq_common
VEC 34,0,irq_common
VEC 35,0,irq_common
VEC 36,0,irq_common
VEC 37,0,irq_common
VEC 38,0,irq_common
VEC 39,0,irq_common
VEC 40,0,irq_common
VEC 41,0,irq_common
VEC 42,0,irq_common
VEC 43,0,irq_common
VEC 44,0,irq_common
VEC 45,0,irq_common
VEC 46,0,irq_common
VEC 47,0,irq_common

trap_common:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    pushl %esp
    call _trap_handler
1:
    cli
    hlt
    jmp 1b

irq_common:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%fs
    movw %ax,%gs
    pushl %esp
    call _irq_handler
    addl $4,%esp
    /* Preemption point: if the tick set need_resched, switch stacks
     * now (the new thread resumes here later and tears down + irets
     * into its own interrupted code). */
    movl _sched_need_resched, %eax
    testl %eax, %eax
    jz 2f
    call _sched_do_switch
2:
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8,%esp
    iret

.data
.globl irq_stub_table
.globl _irq_stub_table
irq_stub_table:
_irq_stub_table:
.long irq_stub_0
.long irq_stub_1
.long irq_stub_2
.long irq_stub_3
.long irq_stub_4
.long irq_stub_5
.long irq_stub_6
.long irq_stub_7
.long irq_stub_8
.long irq_stub_9
.long irq_stub_10
.long irq_stub_11
.long irq_stub_12
.long irq_stub_13
.long irq_stub_14
.long irq_stub_15
.long irq_stub_16
.long irq_stub_17
.long irq_stub_18
.long irq_stub_19
.long irq_stub_20
.long irq_stub_21
.long irq_stub_22
.long irq_stub_23
.long irq_stub_24
.long irq_stub_25
.long irq_stub_26
.long irq_stub_27
.long irq_stub_28
.long irq_stub_29
.long irq_stub_30
.long irq_stub_31
.long irq_stub_32
.long irq_stub_33
.long irq_stub_34
.long irq_stub_35
.long irq_stub_36
.long irq_stub_37
.long irq_stub_38
.long irq_stub_39
.long irq_stub_40
.long irq_stub_41
.long irq_stub_42
.long irq_stub_43
.long irq_stub_44
.long irq_stub_45
.long irq_stub_46
.long irq_stub_47
