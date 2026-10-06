# ADR-0058: Blocking sock_recv — park, wake, expire (15-ii)

Status: accepted (Phase 15, step ii). Date: 2026-10-06.

## Context

`sock_recv` spun on `g_ticks` to its timeout: a waiter
burned its whole slice even with nothing to do, and any
lower-prio waiter starved outright. The 15-i primitives
(BLOCKED + sleep/wakeup + timer expiry) existed with no
callers; sockets are the first.

## Decision

- `sock_recv` is now three-tier: fast path (data present,
  any context) -> poll (`timeout == 0`, immediate EAGAIN,
  frozen-tick safe) -> early-boot spin (scheduler dead:
  pre-scheduler selftests unchanged) -> blocking park
  (deadline sleep; delivery/close/expiry wakes).
- Per-socket waiter + bind generation: delivery wakes with
  data (field cleared before waking), close wakes into
  EBADF, rebind under a sleeper fails it via gen mismatch.
  One waiter per socket (second sleeper is EBUSY, never
  stranded). Wake-after-gone is safe both directions.
- Proof split by file (no host-guard precedent in-tree):
  `sock.c` keeps the mechanism (host-linked; T21 covers
  the spin path), `kernel/net/sock_block.c` holds the
  threaded proof guest-only (`SOCKBLK-OK`): parked waiter
  woken by loopback send (5 bytes exact), +30 expiry
  in-window, close-under-waiter EBADF.
- `sched_can_sleep()` (cli/sti read of started+current)
  is the spin/block branch; stale reads fail safe (spin
  instead of sleep).

## Evidence

Guest `SOCK-OK` + `SOCKBLK-OK`; host T21 unchanged
verdicts (spin path); `nova test` 21/21 (no ABI change).

## Hard lessons

- Threaded proofs can't host-link: first attempt kept
  the proof in `sock.c` and broke T21 (thread_* undefined
  on host). Fix is placement (guest-only file, same split
  as `sched.c`), not stubs -- stubbing threads would pass
  vacuously and prove nothing.
- T21 still needed two honest stubs (`can_sleep=0` +
  unreached `sched_current_thread`): the blocking path
  references them, never executes. Documented unreached,
  like the frozen-tick spin it guards.
- Close-then-park ordering in the proof is load-bearing:
  closing first means the worker never parks (fast EBADF)
  and the BLOCKED wait watchdogs. Close UNDER the parked
  worker, not before creating it.

## Consequences

- Loopback RX blocks properly; wire RX still spins in
  `net_udp_recv` (no socket demux yet -- when it lands,
  delivery will wake from IRQ context, which is why
  wakeup was built IRQ-safe).
- Nothing else sleeps yet (keyboard/getkey, disk waits
  are future callers).
