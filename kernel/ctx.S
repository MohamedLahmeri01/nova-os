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
    movl %esp, (%eax)
    movl (%ecx), %esp
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

/* Minimal setjmp/longjmp for the user-mode roundtrip (Phase 4b).
 * Buffer: ebx, esi, edi, ebp, esp(pre-call), eip. setjmp stores
 * ESP+4 (post-call balance, keeps 16B alignment); longjmp restores
 * and jumps (val==0 becomes 1). */
.globl _ksetjmp
_ksetjmp:
    movl 4(%esp), %eax
    movl %ebx, (%eax)
    movl %esi, 4(%eax)
    movl %edi, 8(%eax)
    movl %ebp, 12(%eax)
    leal 4(%esp), %ecx
    movl %ecx, 16(%eax)
    movl (%esp), %ecx
    movl %ecx, 20(%eax)
    xorl %eax, %eax
    ret

.globl _klongjmp
_klongjmp:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    movl (%eax), %ebx
    movl 4(%eax), %esi
    movl 8(%eax), %edi
    movl 12(%eax), %ebp
    movl 16(%eax), %esp
    movl 20(%eax), %ecx
    movl %edx, %eax
    testl %eax, %eax
    jnz 3f
    incl %eax
3:
    jmp *%ecx

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
    pushl $0x23
    pushl %edx
    pushl $0x202
    pushl $0x1B
    pushl %ecx
    iret

/* INT 0x81 landing pad (DPL3 gate): CPU pushed user SS/ESP/EFLAGS/
 * CS/EIP on the TSS.ESP0 stack; IF is clear. Build a switchable frame
 * and enter C: the sync path longjmps home, the scheduled path picks
 * the next thread and switches (never returns here). */
.globl _user_ret_stub
_user_ret_stub:
    pusha
    pushfl
    pushl %esp
    call _user_ret_handler
    addl $4, %esp
1:
    cli
    hlt
    jmp 1b
