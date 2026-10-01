/* NOVA OS ring-3 probe (Phase 4b).
 * Copied by the user test into a user-mapped page and entered via
 * iret (never linked for execution here). Self-contained: no calls,
 * no globals, no absolute references — position-independent by
 * construction. Stack on entry: [shared][magic] (pushed by kernel).
 * Reports CS + magic to the shared page, then returns via INT 0x81.
 */
.code32
.text
.globl _user_probe_start
.globl _user_probe_end
_user_probe_start:
    /* iret loads SS/CS only: DS/ES/FS/GS still hold supervisor
     * selectors (DPL0), which fault at CPL3. Reload them first. */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popl %ecx
    popl %edx
    movl %cs, %eax
    movl %eax, (%ecx)
    movl %edx, 4(%ecx)
    int $0x81
1:
    hlt
    jmp 1b
_user_probe_end:
