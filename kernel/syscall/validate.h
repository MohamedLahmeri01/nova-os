/* NOVA OS user-memory validation interface (Phase 5).
 * Two layers: range checks (address bounds, overflow, length caps)
 * plus per-page state (present/user/writable). Page queries come from
 * mem_page_state: the guest walks real tables (vmm.c), the host fuzz
 * harness consults a fake map (same shared logic under test).
 */
#ifndef NOVA_VALIDATE_H
#define NOVA_VALIDATE_H

#include <stdint.h>

int mem_page_state(uint32_t addr, int *present, int *user, int *writable);
int validate_usermem(uint32_t ptr, uint32_t len, int write);

#endif /* NOVA_VALIDATE_H */
