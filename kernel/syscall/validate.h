/* NOVA OS user-memory validation interface (Phase 5).
 * Two layers: range checks (address bounds, overflow, length caps)
 * plus per-page state (present/user/writable). Page queries come from
 * mem_page_state: the guest walks real tables (vmm.c), the host fuzz
 * harness consults a fake map (same shared logic under test).
 * This header is the SINGLE canonical user-pointer policy: no other
 * file walks page tables for access decisions (verified by audit).
 */
#ifndef NOVA_VALIDATE_H
#define NOVA_VALIDATE_H

#include <stdint.h>

int mem_page_state(uint32_t addr, int *present, int *user, int *writable);
int validate_usermem(uint32_t ptr, uint32_t len, int write);
/* Centralized copies (0.3-B): validate-once, copy-once, same errno
 * discipline. All syscall payload movement uses these except
 * copy_user_path (NUL-scan exception, single-pass by construction). */
int copy_from_user(void *kdst, uint32_t usrc, uint32_t len);
int copy_to_user(uint32_t udst, const void *ksrc, uint32_t len);

#endif /* NOVA_VALIDATE_H */
