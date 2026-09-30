/* NOVA OS panic handler: COM1 register dump + EBP stack walk (Phase 1).
 * Single-core assumption is explicit (cpu=0); SMP will extend this.
 * The walk is defensive: alignment, range, kernel-text, monotonic-unwind
 * and frame-count guards, so a corrupt stack ends the trace, not the
 * handler. Stage2 zeroes EBP, terminating the chain cleanly.
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"

#define PANIC_MAX_FRAMES 8u
#define STACK_LO 0x5000u
#define STACK_HI 0x91000u
#define KTEXT_LO 0x10000u
#define KTEXT_HI 0x90000u

__attribute__((noreturn)) void nova_panic(const char *reason) {
    uint32_t *sp;
    uint32_t eflags, cr0, cr2, cr3;

    __asm__ volatile("cli");
    /* Snapshot GP regs; the pushed image stays readable below ESP
     * (interrupts off, no calls before copying). PUSHA pushes in order
     * AX CX DX BX SP BP SI DI, so with the stack growing down the memory
     * order low->high is DI SI BP SP BX DX CX AX. Index accordingly. */
    __asm__ volatile("pusha; movl %%esp, %0; addl $32, %%esp"
                     : "=r"(sp)::"memory");
    uint32_t edi = sp[0], esi = sp[1], ebp_s = sp[2], esp0 = sp[3];
    uint32_t ebx = sp[4], edx = sp[5], ecx = sp[6], eax = sp[7];
    (void)ebp_s;
    uint32_t eip = (uint32_t)__builtin_return_address(0);
    uint32_t ebp = (uint32_t)__builtin_frame_address(0);
    __asm__ volatile("pushfl; popl %0" : "=r"(eflags));
    __asm__ volatile("movl %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("movl %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("movl %%cr3, %0" : "=r"(cr3));

    serial_puts("PANIC: ");
    serial_puts(reason);
    serial_putc('\n');
    serial_puts("cpu=0\n");
    serial_puts("eip=0x");
    serial_puthex32(eip);
    serial_puts(" eflags=0x");
    serial_puthex32(eflags);
    serial_putc('\n');
    serial_puts("eax=");
    serial_puthex32(eax);
    serial_puts(" ebx=");
    serial_puthex32(ebx);
    serial_puts(" ecx=");
    serial_puthex32(ecx);
    serial_puts(" edx=");
    serial_puthex32(edx);
    serial_putc('\n');
    serial_puts("esi=");
    serial_puthex32(esi);
    serial_puts(" edi=");
    serial_puthex32(edi);
    serial_puts(" ebp=");
    serial_puthex32(ebp);
    serial_puts(" esp=");
    serial_puthex32(esp0);
    serial_putc('\n');
    serial_puts("cr0=");
    serial_puthex32(cr0);
    serial_puts(" cr2=");
    serial_puthex32(cr2);
    serial_puts(" cr3=");
    serial_puthex32(cr3);
    serial_putc('\n');
    serial_puts("STACK:\n");
    for (uint32_t f = 0; f < PANIC_MAX_FRAMES && ebp != 0; f++) {
        if ((ebp & 3u) || ebp < STACK_LO || ebp > STACK_HI) {
            break;
        }
        uint32_t *frame = (uint32_t *)ebp;
        uint32_t next_ebp = frame[0];
        uint32_t ret = frame[1];
        if (ret < KTEXT_LO || ret > KTEXT_HI) {
            break;
        }
        serial_puts("  #");
        serial_putdec32(f);
        serial_puts(" eip=0x");
        serial_puthex32(ret);
        serial_puts(" ebp=0x");
        serial_puthex32(ebp);
        serial_putc('\n');
        if (next_ebp == 0 || next_ebp <= ebp) {
            break; /* clean end (stage2 zeroes EBP) or corrupt chain */
        }
        ebp = next_ebp;
    }
    serial_puts("END-PANIC-HALT\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
