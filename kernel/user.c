/* NOVA OS user-mode roundtrip test (Phase 4b).
 * Builds a throwaway address space (kernel low cloned, one user PT),
 * copies a self-contained probe to a user page, and enters ring 3 via
 * iret. The probe reports CS + magic through a shared page and returns
 * via INT 0x81 (DPL3 gate) -> CR3 restore -> longjmp home. All locals
 * used after the jump are assigned before ksetjmp (setjmp discipline).
 * Timer IRQs during user mode exercise TSS.ESP0 transparently.
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/addrspace.h"
#include "gdt.h"
#include "irq/irq.h"
#include "user.h"
#include "process/process.h"

#if defined(__i386__)
#include "arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "arch/x86_64/cpu.h"
#else
#error "user: no arch_cpu backend for this architecture"
#endif

#define USER_CODE 0x80000000u
#define USER_SHARED 0x80001000u
#define USER_STACK_TOP 0x80400000u
#define USER_MAGIC 0x55534552u

extern uint8_t user_probe_start[];
extern uint8_t user_probe_end[];
extern void user_ret_stub(void);
extern int ksetjmp(uint32_t *buf);
extern void klongjmp(uint32_t *buf, uint32_t val);

static uint32_t g_jmp[6];
static uint32_t g_saved_kpd;

void user_ret_handler(void) {
    arch_write_cr3(g_saved_kpd);
    klongjmp(g_jmp, 1);
    nova_panic("longjmp-returned");
}

int user_selftest(void) {
    struct process *p;
    struct addrspace *as;
    uint32_t code_f, shared_f, s_base, tss_stack;
    uint32_t probe_len;
    uint32_t user_esp;
    probe_len = (uint32_t)(user_probe_end - user_probe_start);
    gdt_init();
    p = process_create();
    as = addrspace_create();
    if (p == 0 || as == 0) {
        nova_panic("user-no-proc");
    }
    p->cr3 = as->pd;
    code_f = pmm_alloc_frame();
    shared_f = pmm_alloc_frame();
    s_base = pmm_alloc_contig(2);
    tss_stack = pmm_alloc_contig(2);
    if (code_f == 0 || shared_f == 0 || s_base == 0 || tss_stack == 0) {
        nova_panic("user-no-mem");
    }
    addrspace_map(as, USER_CODE, code_f, 1);
    addrspace_map(as, USER_SHARED, shared_f, 1);
    addrspace_map(as, USER_STACK_TOP - 8192u, s_base, 1);
    addrspace_map(as, USER_STACK_TOP - 4096u, s_base + 4096u, 1);
    for (uint32_t i = 0; i < probe_len; i++) {
        ((uint8_t *)code_f)[i] = user_probe_start[i];
    }
    *(uint32_t *)(shared_f) = 0;
    *(uint32_t *)(shared_f + 4u) = 0;
    *(uint32_t *)(s_base + 8192u - 4u) = USER_MAGIC;
    *(uint32_t *)(s_base + 8192u - 8u) = USER_SHARED;
    /* NOTE: the writes above use PHYSICAL addresses (identity map),
     * but the user ESP must be the VIRTUAL stack top. */
    user_esp = USER_STACK_TOP - 8u;
    idt_set_gate(0x81, user_ret_stub, 0xEEu);
    tss_set_esp0(tss_stack + 8192u);
    tss_load();
    g_saved_kpd = arch_read_cr3();
    if (ksetjmp(g_jmp) == 0) {
        arch_write_cr3(as->pd);
        __asm__ volatile("pushl $0x23; pushl %0; pushl $0x202; "
                         "pushl $0x1B; pushl %1; iret" ::"r"(user_esp),
                         "r"(USER_CODE)
                         : "memory");
        nova_panic("user-iret-returned");
    }
    {
        uint32_t *sh = (uint32_t *)shared_f;
        if (sh[0] != 0x1Bu || sh[1] != USER_MAGIC) {
            nova_panic("user-verify-fail");
        }
    }
    serial_puts("USER cs=0x1b magic-ok\nUSER-OK\n");
    pmm_free_frame(code_f);
    pmm_free_frame(shared_f);
    pmm_free_contig(s_base, 2);
    pmm_free_contig(tss_stack, 2);
    addrspace_destroy(as);
    kfree(p);
    return 0;
}
