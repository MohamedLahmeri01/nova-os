/* NOVA OS context switch + thread trampoline (Phase 4a).
 * ctx_switch(old_sp, new_sp): pusha/pushfl, stash ESP, load new ESP,
 * popfl/popa, ret into the new context. Fresh threads start in
 * thread_trampoline with [fn][arg] atop their stack (built by
 * thread_create); it calls fn(arg) then thread_exit. Freestanding
 * -m32; C symbols carry the mingw underscore (see TOOLCHAIN.md).
 */
.code32
.text

.globl _ctx_switch
_ctx_switch:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    pusha
    pushfl
    /* Segments are per-thread state too: a user thread preempted
     * while DS=0x23 must resume with 0x23, not whatever kernel
     * thread ran meanwhile (and vice versa). */
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movl %esp, (%eax)
    movl (%ecx), %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popfl
    popa
    ret

.globl _thread_trampoline
_thread_trampoline:
    movl (%esp), %eax
    pushl 4(%esp)
    call *%eax
    addl $4, %esp
    pushl %eax
    call _thread_exit
2:
    hlt
    jmp 2b

/* First entry into a scheduled user thread (Phase 4c).
 * Arrived via popfl/popa/ret like any fresh context; stack holds
 * [pd][eip][uesp][kstack][tss_slot] (pushed by thread_create_user).
 * Uniform with all other resumes: no layout branches anywhere. */
.globl _user_entry_trampoline
_user_entry_trampoline:
    popl %eax
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    movl %eax, %cr3
    movl %esi, (%edi)
    /* Establish user data segments: iret loads only SS/CS, so a fresh
     * user thread would otherwise run its first instructions with
     * supervisor DS/ES/FS/GS (instant #GP at CPL3). */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl $0x23
    pushl %edx
    pushl $0x202
    pushl $0x1B
    pushl %ecx
    iret
