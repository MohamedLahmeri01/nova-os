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
1:
    hlt
    jmp 1b
