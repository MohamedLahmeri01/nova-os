# ADR-0062: Blocking getkey + waiter discipline (16-i)

Status: accepted (Phase 16, step i). Date: 2026-10-06.

## Context

The shell's `getchar` spun `getkey` + yield per keystroke:
a syscall plus a context switch for every empty poll, on
every idle machine, forever. The sleep/wakeup primitives
(15-i) had exactly one caller family (sockets); keyboard
was the next spin on the list (disk waits are bounded
polls, not idle spins -- see consequences).

## Decision

- `kbd_getkey` blocks when scheduled (fast path first,
  `-EAGAIN` only pre-scheduler, `-EBUSY` on a second
  parker). One waiter, woken from `buf_push` (the IRQ
  fill point). Zero userspace changes: the shell idles
  parked from this boot on.
- `kbd_feed(sc)` splits the translator from the port
  read (mouse_feed pattern); the IRQ half is one line.
- `kbd_block_selftest` (`KBDBLK-OK` gate): parked reader
  woken by fed 'h'/'i' in order, then a second parker
  proves EBUSY (main refused, worker released by Enter).
- Waiter discipline, fixed in both waiters (kbd new,
  sock hardened same step): check-to-park holds IF=0
  (lost-wakeup: a tick/IRQ delivery between check and
  park wakes nobody and strands the sleeper), wakers
  wake-then-clear (a newcomer in between sees a waiter
  instead of stealing the wake), parkers never overwrite
  another waiter (`!= me` or EBUSY) and never clear on
  resume (stale only on expiry, cleared by owner check).
  Sock's deadline always self-healed its version; kbd
  has no timeout, so the hang would have been permanent.

## Evidence

Guest `KBDBLK-OK`; sock suite unbroken (hardened paths
re-proven by SOCKBLK-OK); `nova test` 21/21 (no ABI
change: same number, documented semantics).

## Hard lessons

- No-timeout sleepers raise every waiter race from
  "bounded weirdness" to "permanent hang": audit the
  full check/park/wake/clear matrix, not just the happy
  path, whenever the deadline goes away.
- A first-draft edit that blended sock text into the kbd
  loop was rejected by exact-match (good); the tool
  refusing to guess is a feature -- re-read, then write.

## Consequences

- Idle machines now idle: shell parked, timer ticks only.
- Disk waits stay polling (bounded, short, no idle case).
- Next: 0.2.0 release pass (version bump, dist, notes).
