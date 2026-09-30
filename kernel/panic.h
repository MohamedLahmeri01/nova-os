/* NOVA OS panic handler interface (Phase 1).
 * nova_panic prints diagnostics over serial and never returns.
 * Format (no stability promise yet; EIPs are numeric — no symbol
 * resolution until the debugging story lands):
 *
 *   PANIC: <reason>
 *   cpu=<n>
 *   eip=0x........ eflags=0x........
 *   eax=........ ebx=........ ecx=........ edx=........
 *   esi=........ edi=........ ebp=........ esp=........
 *   cr0=........ cr2=........ cr3=........
 *   STACK:
 *     #<f> eip=0x........ ebp=0x........
 *   END-PANIC-HALT
 */
#ifndef NOVA_PANIC_H
#define NOVA_PANIC_H

__attribute__((noreturn)) void nova_panic(const char *reason);

#endif /* NOVA_PANIC_H */
