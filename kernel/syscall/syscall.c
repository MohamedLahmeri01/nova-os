/* NOVA OS syscall dispatch + handlers (Phase 5, ABI v1).
 * Table-driven (new versions add rows). Every pointer/length goes
 * through validate_usermem; failures return -errno, never panic.
 * Handler costs are bounded (print is cli-guarded, no shared buffer).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "syscall/syscall.h"
#include "syscall/validate.h"
#include "mm/pmm.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "irq/irq.h"
#include "input/kbd.h"
#include "time/time.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#include "../arch/x86/io.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/io.h"
#else
#error "syscall: no arch backend for this architecture"
#endif

extern void syscall_stub(void);

static uint32_t g_sysc_yield;
static uint32_t g_sysc_print;
static uint32_t g_sysc_exit;

void syscall_stats(uint32_t *yld, uint32_t *prt, uint32_t *ext) {
    if (yld) {
        *yld = g_sysc_yield;
    }
    if (prt) {
        *prt = g_sysc_print;
    }
    if (ext) {
        *ext = g_sysc_exit;
    }
}

static int32_t do_yield(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    g_sysc_yield++;
    sched_yield();
    return NOVA_ESUCCESS;
}

static int32_t do_exit(uint32_t code, uint32_t b, uint32_t c) {
    struct thread *cur;
    struct thread *next;
    (void)b;
    (void)c;
    g_sysc_exit++;
    cur = sched_current_thread();
    if (cur == 0) {
        return -NOVA_EINVAL;
    }
    if (cur->is_user) {
        if (cur->user_stack != 0) {
            pmm_free_contig(cur->user_stack, 2);
        }
        if (cur->user_shared != 0) {
            pmm_free_frame(cur->user_shared);
        }
    }
    (void)code;
    thread_zombie_handoff(cur);
    next = sched_pick(cur);
    if (next == 0 || next == cur) {
        nova_panic("sched-empty");
    }
    sched_switch_to(cur, next);
    nova_panic("sys-exit-returned");
}

static int32_t do_print(uint32_t ptr, uint32_t len, uint32_t c) {
    int rc;
    (void)c;
    g_sysc_print++;
    if (len > SYSCALL_MAX_PRINT) {
        return -NOVA_EINVAL;
    }
    rc = validate_usermem(ptr, len, 0);
    if (rc != 0) {
        return rc;
    }
    __asm__ volatile("cli" ::: "memory");
    for (uint32_t i = 0; i < len; i++) {
        serial_putc((char)*(volatile uint8_t *)(ptr + i));
    }
    __asm__ volatile("sti" ::: "memory");
    return (int32_t)len;
}

static int32_t do_getpid(uint32_t a, uint32_t b, uint32_t c) {
    struct thread *cur;
    (void)a;
    (void)b;
    (void)c;
    cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return -NOVA_EINVAL;
    }
    return (int32_t)cur->proc->pid;
}

static int32_t do_gettid(uint32_t a, uint32_t b, uint32_t c) {
    struct thread *cur;
    (void)a;
    (void)b;
    (void)c;
    cur = sched_current_thread();
    if (cur == 0) {
        return -NOVA_EINVAL;
    }
    return (int32_t)cur->id;
}

static int32_t do_version(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return (int32_t)NOVA_ABI_VERSION;
}

static int32_t do_getkey(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return kbd_getkey();
}

static int32_t do_meminfo(uint32_t total_ptr, uint32_t free_ptr,
                          uint32_t c) {
    uint32_t *t;
    uint32_t *f;
    (void)c;
    if (validate_usermem(total_ptr, 4, 1) != 0 ||
        validate_usermem(free_ptr, 4, 1) != 0) {
        return -NOVA_EFAULT;
    }
    t = (uint32_t *)total_ptr;
    f = (uint32_t *)free_ptr;
    *t = pmm_total_frames();
    *f = pmm_free_count();
    return NOVA_ESUCCESS;
}

static int32_t do_ticks(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return (int32_t)g_ticks;
}

typedef int32_t (*sys_fn_t)(uint32_t, uint32_t, uint32_t);

static const struct {
    uint32_t nr;
    const char *name;
    sys_fn_t fn;
} sys_table[] = {
    { SYS_YIELD, "yield", do_yield },
    { SYS_EXIT, "exit", do_exit },
    { SYS_PRINT, "print", do_print },
    { SYS_GETPID, "getpid", do_getpid },
    { SYS_GETTID, "gettid", do_gettid },
    { SYS_VERSION, "version", do_version },
    { SYS_GETKEY, "getkey", do_getkey },
    { SYS_MEMINFO, "meminfo", do_meminfo },
    { SYS_TICKS, "ticks", do_ticks },
};

void syscall_handler(struct syscall_frame *f) {
    uint32_t nr = f->eax;
    for (uint32_t i = 0;
         i < sizeof(sys_table) / sizeof(sys_table[0]); i++) {
        if (sys_table[i].nr == nr) {
            f->eax = (uint32_t)sys_table[i].fn(f->ebx, f->ecx, f->edx);
            return;
        }
    }
    f->eax = (uint32_t)(-NOVA_ENOSYS);
}

void syscall_init(void) {
    idt_set_gate(0x80, syscall_stub, 0xEEu);
    /* Permanent hygiene: the gate must resolve to the stub. */
    {
        arch_idtr_t idtr;
        uint32_t *gate;
        arch_store_idt(&idtr);
        gate = (uint32_t *)(idtr.base + 0x80u * 8u);
        if ((gate[0] & 0xFFFFu) != ((uint32_t)syscall_stub & 0xFFFFu) ||
            (gate[1] >> 16) != ((uint32_t)syscall_stub >> 16) ||
            ((gate[1] >> 8) & 0xFFu) != 0xEEu) {
            nova_panic("syscall-gate-fail");
        }
    }
}
