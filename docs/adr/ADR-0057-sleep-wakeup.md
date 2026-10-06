# ADR-0057: Sleep/wakeup primitives — blocking foundation (15-i)

Status: accepted (Phase 15, step i). Date: 2026-10-06.

## Context

`sock_recv` spins on `g_ticks` (busy-poll to timeout):
correct, but burns the waiter's whole slice and forbids
any use where the waiter isn't the highest-prio thread.
True blocking needs a BLOCKED state, a sleep list, timer
expiry, and wakeup -- none existed (READY/RUNNING/ZOMBIE
only). APs halt with IRQs off, so single-core list
discipline (cli/sti, like yield/exit) suffices.

## Decision

- `THREAD_BLOCKED` + three fields (`sleep_deadline`,
  `sleep_timed`, `sleep_timedout`); `next` links the
  sleep list while blocked (sleeper-owned, off runqueue).
  All three thread creators init them (kmalloc doesn't
  zero -- uninit flags would fake deadlines).
- `sched_sleep()` / `sched_sleep_until(deadline)` (thread
  context): leave runqueue, push sleep list, switch away;
  resume returns the verdict (0 woken, 1 expired).
  Sleeping last panics like sched-empty (fail-fast).
- `sched_wakeup(t)` (thread + IRQ context, stores only):
  no-op unless BLOCKED (wake-after-gone safe); higher-prio
  wakeups set need_resched (strict priority at IRQ return).
- `sched_tick` expires timed sleepers (signed compare:
  wrap-safe) before accounting.
- `sched_sleep_selftest` (`SLEEP-OK` gate, after
  sched_selftest when scheduler + timer live): worker
  parks far-deadline x2 (main wakes early, rc 0), then
  30 ticks unwoken (rc 1, window 30..500). No sock
  integration yet -- primitives prove alone first.

## Evidence

Guest `SLEEP-OK`; `nova test` 21/21 (no ABI change).

## Hard lessons

- C definition order bites across one file: `sched_tick`
  needed the sleep list + waker, both first written after
  `sched_yield`. Fix is placement (block before tick),
  not forward decls -- keep the file readable top-down.
- `g_ticks` extern lived mid-file (selftest use); the
  tick walk needed it earlier. Promoted to file top,
  redundant mid-file copy removed.
- Edit discipline around struct init: one attempted
  field-init edit ate the adjacent `if` line; caught by
  immediate `git diff` review before building. Diff every
  edit that touches control flow, no exceptions.

## Consequences

- Nothing sleeps yet (no callers): `sock_recv` still
  spins. Next step wires recv onto sleep/wakeup with its
  timeout arg as the deadline (loopback delivery wakes).
- Infinite-timeout recv (`timeout` wrap/large) maps to
  `sched_sleep` semantics via untimed wait -- decided at
  wiring time.
