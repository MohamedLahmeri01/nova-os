/* NOVA OS process capabilities (Phase 10a, sandbox primitive).
 * A bitmask per process gates privileged syscalls (network,
 * spawning). Drops are monotonic: no interface raises caps, children
 * inherit at spawn, init starts with all. Pure logic (host-testable);
 * enforcement reads the calling thread's process (boot context with
 * no current process allows, so early selftests keep working).
 */
#ifndef NOVA_CAPS_H
#define NOVA_CAPS_H

#include <stdint.h>

#include "syscall/syscall.h"

#define CAP_NET 0x01u   /* network transmit/reconfig (ping, dhcp) */
#define CAP_SPAWN 0x02u /* process creation (exec/spawn) */
#define CAPS_ALL (CAP_NET | CAP_SPAWN)

#define CAP_OP_GET 0u
#define CAP_OP_DROP 1u

#define UID_ROOT 0u
#define UID_NOBODY 65534u

/* 0 if caps covers need, -EPERM if a right is missing, -EINVAL if
 * need names unknown bits (caller bug, not a denial). */
int caps_allow(uint32_t caps, uint32_t need);

/* Monotonic drop: out = caps & ~mask. Unknown mask bits refuse
 * (-EINVAL: typos must not silently pass). */
int caps_drop(uint32_t caps, uint32_t mask, uint32_t *out);

/* Deterministic sandbox proof (guest selftest): a capless scratch
 * process attempts gated calls through real dispatch; prints
 * CAPS-OK. Nonzero on failure (fatal: panic). */
int caps_selftest(void);

#endif /* NOVA_CAPS_H */
