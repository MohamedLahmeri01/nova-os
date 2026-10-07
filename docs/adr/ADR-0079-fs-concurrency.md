# ADR-0079: Filesystem concurrency — VFS big lock (v0.5 Phase H)

Status: accepted (v0.5). Date: 2026-10-07.
Supersedes the runtime contract of ADR-0076 (which remains
as the pre-locking record; its review rule is retired).

## Context

ADR-0076 declared concurrent FS use UNSUPPORTED (plain
refcounts, shared buffers/offsets, zero locks). Phase H
audited every object, lifetime, driver cache, disk poll,
sleep path, and IRQ interaction (see
`docs/FILESYSTEM-CONCURRENCY.md`).

## Decision

One global VFS spinlock, irqsave, held across each of 12
FS syscall handlers (open/read/write/close/readdir/mkdir/
exec/fsck/chmod/unlink/rmdir/chroot). Alternatives
considered: per-inode/file locks (rejected: nesting +
lifetime complexity for zero measured need); plain spin
without irqsave (rejected: same-priority waiter could
starve a preempted holder — deadlock by design); lock
inside each `fs_*` (rejected: `resolve_parent` nests
lookup, non-reentrant deadlock).

## Evidence

`FSSTRESS-OK`: same-prio pair through real dispatch —
open/close ×200, shared-fd byte-exact sums (8192),
atomic 64B blocks, metadata storm (valid rc only),
3-graft byte-identical reads, refs drained, fsck clean,
~110 ticks. Full suite 23/23 unchanged.

## Hard lessons

- The stress found a REAL deadlock first: ATA blind `sti`
  reopened preemption inside the critical section; fixed
  to save/restore (4 sites). Without the battery, the
  lock would have shipped deadlock-prone.
- Borrowed user mappings are not switch-safe: every
  context switch reloads kernel CR3 for kernel threads.
  Test borrows must be per-call under IF held (test-design
  rule, documented).

## Consequences

- Concurrent FS syscalls are SUPPORTED with atomic-call
  semantics (no POSIX ordering promises beyond that).
- Direct `fs_*` callers stay single-threaded (review rule).
- FS-sharing threads should share priority (no inheritance).
- SECURITY-MODEL W8 closes.
