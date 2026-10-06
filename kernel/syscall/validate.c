/* NOVA OS user-memory validation (Phase 5, centralized copies 0.3-B).
 * Range first (bounds, wraparound, per-call cap), then every page.
 * Returns 0 or negative -errno (never panics: hostile input is data).
 *
 * TOCTOU position (precise): mappings are immutable once created
 * (no munmap/mprotect/demand-paging anywhere in the tree), so a
 * validated range cannot fault-escape later: present/user/writable
 * bits only ever gain, never lose. What a sibling thread CAN do is
 * rewrite buffer CONTENTS mid-copy (data tearing). The defense is
 * single-pass snapshots: every copy below reads each source byte
 * exactly once into kernel memory, and all kernel decisions run on
 * the snapshot. The residual semantic (operating on a different-
 * but-valid input) matches any OS without frozen pages and is
 * accepted; fault-escape is structurally impossible. THIS INVARIANT
 * IS LOAD-BEARING: introducing munmap/mprotect/demand-paging must
 * re-audit every call site (see ADR-0066).
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

/* Centralized user copies (Phase 0.3-B): validate once, copy once.
 * Every syscall data movement funnels here except copy_user_path
 * (NUL-scan cannot pre-validate a length; it stays a documented
 * single-pass exception). Callers snapshot structs/scalars first,
 * then move payloads through these; kernel decisions run on the
 * snapshot only. Same errno discipline as the validator. */
int copy_from_user(void *kdst, uint32_t usrc, uint32_t len) {
    uint8_t *d = (uint8_t *)kdst;
    int rc;
    if (kdst == 0 && len != 0) {
        return -NOVA_EINVAL;
    }
    rc = validate_usermem(usrc, len, 0);
    if (rc != 0) {
        return rc;
    }
    for (uint32_t i = 0; i < len; i++) {
        d[i] = *(volatile uint8_t *)(usrc + i);
    }
    return NOVA_ESUCCESS;
}

int copy_to_user(uint32_t udst, const void *ksrc, uint32_t len) {
    const uint8_t *s = (const uint8_t *)ksrc;
    int rc;
    if (ksrc == 0 && len != 0) {
        return -NOVA_EINVAL;
    }
    rc = validate_usermem(udst, len, 1);
    if (rc != 0) {
        return rc;
    }
    for (uint32_t i = 0; i < len; i++) {
        *(volatile uint8_t *)(udst + i) = s[i];
    }
    return NOVA_ESUCCESS;
}
