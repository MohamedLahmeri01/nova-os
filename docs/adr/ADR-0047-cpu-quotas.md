# ADR-0047: CPU quotas — tick accounting + preemption kill (13b)

Status: accepted (Phase 13b). Date: 2026-10-05.

## Context

Limits (10b) bounded space (fds, bytes, pages) but not time: a
spinning user thread ran forever. Deferred then because killing
from the preemption path needs kill semantics the scheduler has
since earned (zombie handoff + graveyard + noreturn exit, all
lock-free, all pre-existing).

## Decision

- Per-process `ticks_used` (every tick, whoever runs) +
  `rlimit_ticks` (default unbounded; lower-only `SYS_RLIMIT`,
  shell `rlimit ticks`). `sched_do_switch` diverts over-limit
  *user* threads to `thread_exit` (same footing as `do_exit`:
  own stack current, user frames freed the same way, noreturn
  switch away; addrspace/code leak like every user exit today).
  Kernel threads exempt (trusted); default-unlimited never trips.
- `sched_ticklimit_selftest` spins a prio-1 user thread
  (`EB FE`) against limit 50: preempts the prio-0 boot thread
  immediately, dies at the first post-quota switch, observed via
  `thread_count`; asserts ticks_used in [50, 200] (slice is 10,
  margin is wide). Prints `TICK-OK`, gated both lists. Fixtures
  freed (code/stack/addrspace/proc): leak-free, unlike exits.

## Evidence

Guest `TICK-OK`; `nova test` 20/20.

## Hard lessons

- Starvation is determinism: the spinner outranking the observer
  removes all polling races (the boot thread cannot run until
  the kill lands). Design tests so the expected event unblocks
  the asserter, not the reverse.
- `thread_exit` takes no thread (uses current): callable from
  the switch tail precisely because current is still the victim
  there. Read the callee before assuming context mismatch.
- Counters beat flags for death-watching: the handoff
  decrements before reaping, so `thread_count` observes death
  without waiting out the graveyard.

## Consequences

- No signals, no exit codes observed, no process reaping
  (addrspace/code frames of killed threads leak like normal
  exits); quotas bind user threads only.
- Next: 13c candidates (sockets/DNS, on-disk chmod, native uid
  passthrough, IRQ-driven virtio) or release.
