# ADR-0076: Filesystem single-threaded contract (v0.5 Phase A)

Status: accepted (v0.5). Date: 2026-10-06.

## Context

The v0.5 Phase A re-triage inspected every FS concurrency
primitive and found none: refcounts are plain `++`/`--`
(`ramfs.c`), node data buffers and fd offsets are shared
without guards, and no FS path holds a lock or an IF
section. This is safe today for exactly one reason: no
two threads ever use the filesystem concurrently (the
shell is single-threaded; selftests run sequentially;
bench file churn runs on the boot thread).

## Decision

- **FS syscalls are single-threaded-only until locking
  lands.** Concurrent FS use from two threads (same or
  different processes) is UNSUPPORTED and may corrupt
  refcounts, share buffers mid-I/O, or interleave offsets
  — by contract, not by accident. This is declared here
  so the constraint is explicit instead of merely true.
- **Phase O (concurrent stress) is gated on locking.**
  No stress test runs until at least a global FS lock
  (or finer granularity with its own ADR) exists; running
  stress against unlocked structures would only re-prove
  known races.
- **No code change in this ADR.** Nothing is locked,
  serialized, or restricted at runtime — enforcement
  would cost every current (single-threaded) user for
  no current threat. The contract is documentation +
  review rule.

## Evidence

Grep over `kernel/fs/`: zero spinlock/IF-guard acquisitions
(the only "lock" hits are block-I/O English); refcount
sites listed (`ramfs.c:283/291`, holds at 345/380);
fd tables per-process but shared across threads of one
process (offsets included).

## Hard lessons

- "Safe by current use" rots into "assumed safe" within
  months: the shell could grow job control, a second
  program could spawn threads, and suddenly the
  corruption is a mystery instead of a contract
  violation. Writing it down is the whole fix for now.

## Consequences

- Reviewers reject multithreaded-FS users until locking
  lands (cite this ADR, not vibes).
- The locking design (global first? per-node? journal
  interactions?) gets its own ADR with Phase O.
- `docs/SECURITY-MODEL.md` W8 tracks this as OPEN.
