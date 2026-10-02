/* NOVA OS syscall gate stub (Phase 5).
 * INT 0x80 (DPL3 gate): CPU pushes SS/ESP/EFLAGS/CS/EIP; here builds
 * the C frame (regs + flags + arg) and tears it down after return.
 * No vec/err words (single gate). Symbol spelling per TOOLCHAIN.md.
 */
.code32
.text
.globl _syscall_stub
_syscall_stub:
    pusha
    pushfl
    pushl %esp
    call _syscall_handler
    addl $4, %esp
    popfl
    popa
    iret
