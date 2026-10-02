/* NOVA OS syscall ABI (Phase 5, v1).
 * 32-bit calling convention: EAX = number; EBX, ECX, EDX = args;
 * return non-negative success or negative -errno in EAX.
 * Versioning: ABI_VERSION query + dispatch table (new versions add
 * rows; old rows stay). Numbers are stable within a major version.
 */
#ifndef NOVA_SYSCALL_H
#define NOVA_SYSCALL_H

#include <stdint.h>

#define NOVA_ABI_VERSION 1u

#define SYS_YIELD 0u
#define SYS_EXIT 1u
#define SYS_PRINT 2u
#define SYS_GETPID 3u
#define SYS_GETTID 4u
#define SYS_VERSION 5u
#define SYS_MAX 5u

/* Linux-compatible errno values (negative on return). */
#define NOVA_ESUCCESS 0
#define NOVA_EFAULT 14
#define NOVA_EINVAL 22
#define NOVA_ENOSYS 38

#define SYSCALL_MAX_PRINT 1024u
#define SYSCALL_MAX_RANGE 4096u
#define USER_END 0xC0000000u

/* C argument is the pushed ESP *value* (= address of kflags), so the
 * struct starts there: call machinery (retaddr) lives below and is
 * never modeled (adding phantom fields shifts every register). */
struct syscall_frame {
    uint32_t kflags;
    uint32_t edi, esi, ebp, esp_ignored, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, esp, ss;
};

void syscall_init(void);
void syscall_handler(struct syscall_frame *f);
void syscall_stats(uint32_t *yld, uint32_t *prt, uint32_t *ext);

#endif /* NOVA_SYSCALL_H */
