/* NOVA OS resource limits (Phase 10b, synchronous subset).
 * Per-process ceilings for fds, file size, and mapped frames.
 * Sets are monotonic (lower-only: raising is privilege gain);
 * enforcement sits at the allocation site (fd alloc, write, map)
 * and refuses with errno, never panics. Pure logic (host-testable);
 * the process fields live in process/process.h. CPU time is out of
 * scope here: it needs kill-from-preemption semantics (scheduler
 * work, later slice).
 */
#ifndef NOVA_RLIMIT_H
#define NOVA_RLIMIT_H

#include <stdint.h>

#include "syscall/syscall.h"
#include "process/process.h"
#include "fs/fs.h"

#define RLIM_NOFD 0u
#define RLIM_FSIZE 1u
#define RLIM_FRAMES 2u
#define RLIM_TICKS 3u

#define RLIM_OP_GET 0u
#define RLIM_OP_SET 1u

#define RLIM_NOFD_HARD NOVA_NFD
#define RLIM_FSIZE_HARD FS_MAX_FILE
#define RLIM_FRAMES_HARD 524288u /* 512 PDs x 1024: whole user half */
#define RLIM_TICKS_HARD 0xFFFFFFFFu /* no ceiling: unbounded default */

#define RLIM_NOFD_DEF NOVA_NFD
#define RLIM_FSIZE_DEF FS_MAX_FILE
#define RLIM_FRAMES_DEF RLIM_FRAMES_HARD
#define RLIM_TICKS_DEF RLIM_TICKS_HARD

/* Lower-only set rule: out = val. EINVAL for NULL out or val above
 * the hard ceiling (caller bug); EPERM for raising (privilege). */
int rlimit_set_rule(uint32_t cur, uint32_t val, uint32_t hard,
                    uint32_t *out);

/* File-size gate: how many of len bytes at off may proceed under
 * lim (clamped, never grown). EFBIG when off is already at/past
 * the limit (zero-length writes: callers skip the gate). */
int rlimit_fsize_allow(uint32_t lim, uint32_t off, uint32_t len,
                       uint32_t *oklen);

/* Deterministic ceiling proof (guest selftest): SYS_RLIMIT
 * get/lower/raise-refuse, fd/frame ceilings bind; prints RLIM-OK.
 * Nonzero on failure (fatal: panic). */
int rlim_selftest(void);

#endif /* NOVA_RLIMIT_H */
