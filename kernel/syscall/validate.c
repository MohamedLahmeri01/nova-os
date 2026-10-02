/* NOVA OS user-memory validation (Phase 5, shared guest/host).
 * Range first (bounds, wraparound, per-call cap), then every page.
 * Returns 0 or negative -errno (never panics: hostile input is data).
 * Single-core makes the check-then-touch TOCTOU negligible (no
 * concurrent mapper exists yet); revisit with demand paging.
 */
#include <stdint.h>
#include <stddef.h>

#include "syscall/syscall.h"
#include "syscall/validate.h"
#include "mm/addrspace.h"

int validate_usermem(uint32_t ptr, uint32_t len, int write) {
    uint32_t end;
    if (len == 0) {
        return 0;
    }
    if (len > SYSCALL_MAX_RANGE) {
        return -NOVA_EINVAL;
    }
    if (ptr < USER_BASE || ptr >= USER_END) {
        return -NOVA_EFAULT;
    }
    end = ptr + len;
    if (end < ptr || end > USER_END) {
        return -NOVA_EFAULT;
    }
    {
        uint32_t page = ptr & ~0xFFFu;
        uint32_t last = (end - 1u) & ~0xFFFu;
        for (;;) {
            int present = 0, user = 0, writable = 0;
            if (mem_page_state(page, &present, &user, &writable) != 0) {
                return -NOVA_EFAULT;
            }
            if (!present || !user || (write && !writable)) {
                return -NOVA_EFAULT;
            }
            if (page == last) {
                break;
            }
            page += 0x1000u;
        }
    }
    return 0;
}
