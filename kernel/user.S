/* NOVA OS ring-3 probes (Phase 5).
 * Self-contained (no calls/globals, position-independent). Syscall
 * convention: EAX=nr, EBX/ECX/EDX=args, int $0x80, result in EAX.
 * Stack on entry: [shared][magic]. ESI keeps the shared base.
 */
.code32
.text

/* Quick probe (sync test): CS proof, negative print test, hello,
 * magic, exit. */
.globl _user_probe_start
.globl _user_probe_end
_user_probe_start:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popl %esi
    popl %edx
    movl %cs, %eax
    movl %eax, (%esi)
    movl $16, %ecx
    movl $0x90000000, %ebx
    movl $2, %eax
    int $0x80
    movl %eax, 12(%esi)
    pushl $0x0A6968
    movl %esp, %ebx
    movl $3, %ecx
    movl $2, %eax
    int $0x80
    addl $4, %esp
    movl %edx, 4(%esi)
    movl %edx, %ebx
    movl $1, %eax
    int $0x80
1:
    hlt
    jmp 1b
_user_probe_end:

/* Spinning probe (scheduled threads): progress at shared+8,
 * periodic yield, hello, magic, exit. Shared page zeroed pre-entry. */
.globl _user_spin_start
.globl _user_spin_end
_user_spin_start:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popl %esi
    popl %edx
    movl %cs, %eax
    movl %eax, (%esi)
    pushl $0x0A6968
    movl %esp, %ebx
    movl $3, %ecx
    movl $2, %eax
    int $0x80
    addl $4, %esp
    movl $500000000, %ebx
5:
    decl %ebx
    jz 6f
    testl $1023, %ebx
    jnz 5b
    incl 8(%esi)
    testl $16383, %ebx
    jnz 5b
    movl $0, %eax
    int $0x80
    jmp 5b
6:
    movl %edx, 4(%esi)
    movl %edx, %ebx
    movl $1, %eax
    int $0x80
7:
    hlt
    jmp 7b
_user_spin_end:
