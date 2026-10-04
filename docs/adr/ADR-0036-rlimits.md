# ADR-0036: Resource limits — fds, file size, mapped frames

Status: accepted (Phase 10b). Date: 2026-10-04.

## Context

Capabilities (10a) gate WHO may act; nothing bounded HOW MUCH a
process may consume (fds, file growth, address-space size). Phase
10 is sliced: 10b covers the synchronously enforceable limits.
CPU time is explicitly out: killing from the preemption path
(`sched_do_switch` runs on the preempted thread's IRQ stack) needs
kill semantics the scheduler does not have yet (exit codes, exit
reporting, kernel-thread immunity). That is scheduler work for a
later slice, not a gap to paper over.

## Decision

Three per-process ceilings in `struct process` (defaults = hard
ceilings: no behavior change until lowered; spawn inherits):
`RLIMIT_NOFD` (enforced in `fs_fd_alloc` by live count,
`-EMFILE`), `RLIMIT_FSIZE` (clamped in `do_write`: partial writes
allowed up to the limit, `-EFBIG` at/past it, empty writes skip
the gate), `RLIMIT_FRAMES` (counted in `addrspace_map`, which
grew an `int` return: `-ENOSPC` at the ceiling; malformed args
still panic). `SYS_RLIMIT` observes (GET) or lowers (SET:
raising is EPERM, over-hard is EINVAL). Shell `rlimit` shows and
lowers (demo: `rlimit fsize 10` then `put` fails short).
`rlim_selftest` proves ceilings bind through real dispatch
(scratch process + crafted frames under `cli`): get defaults,
lower nofd to 2, third alloc EMFILEs, raise/over-hard/bad-which
refuse, fsize clamp vectors, map/map/refuse on a capped
addrspace; prints `RLIM-OK`, gated in `run-vbox` + `panic-test`.
Host T16 fuzzes the pure set-rule + clamp contract.

## Evidence

Guest `RLIM-OK`; `nova test` 16/16.

## Hard lessons

- New `struct process` fields are read by shared layers
  (`fs_fd_alloc` reads the ceiling): every stack-constructed
  process (`file.c` selftest's `fake`) must init them, or garbage
  denies. Default-deny cuts both ways: audit every constructor,
  including test fixtures.
- `addrspace_map` returning `int` ripples to all mappers: spawn
  checks (a frame-starved child fails whole, never partial);
  test mappers ignore the value (unlimited ceiling).
- The do_write clamp belongs AFTER fd/permission checks but the
  EFBIG must precede any disk mutation (fail before OOM-growth
  paths allocate).

## Consequences

- Lowering below current use is allowed (future allocations
  only); frame tightening binds future spawns (no
  process->addrspace backlink exists to retune live spaces).
- Next: 10c UIDs + file ownership/permissions.
