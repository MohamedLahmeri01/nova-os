# ADR-0061: Tick-driven RX — wire-to-waiter without ISR nesting (15-v)

Status: accepted (Phase 15, step v). Date: 2026-10-06.

## Context

The pump (15-iv) was thread-only: a parked socket waiter
depended on someone else spinning `rx_wait`. The obvious
hook -- pump inside `virtio_net_irq` -- was built first
and then reverted after three consecutive boots failed in
three different places (drill divert, ARP timeout, ping
timeout). This ADR records the failure analysis and the
design that replaced it.

## Decision

- `sched_tick` pumps the ring every tick (after the
  started/current guards, before sleep expiry). 100Hz
  promptness for wire-to-waiter wake; idle cost is two
  ring loads (EAGAIN short-circuits before any MMIO).
  Woken higher-prio waiters preempt at that tick's
  return via the existing need_resched path.
- `virtio_net_irq` is ack + count only (14c form): no
  ring work in the device ISR at all.
- Subsystem lock is IF-saving (unlock restores only what
  its lock found): an ISR-held lock saves IF=0 and never
  re-enables mid-handler. Pump self-locks; queue serve
  locks separately (an IRQ drain between them only
  appends; FIFO kept). `net_rx_pump_locked` serves
  held-lock callers (drill); no nesting anywhere.
- Ring drill (`virtio_net_drill` + `sock_drill_selftest`,
  `IRQPUMP-OK` gate): fake completion on posted buffer 0,
  whole drill under the lock (craft + bump + pump +
  restore), quiet-ring guard (-EBUSY), device progress
  restored (the fake used entry is overwritten by the
  device's next real completion before anyone re-reads).

## Evidence

`IRQPUMP-OK` + `NET-OK` + `SOCKBLK-OK`, three consecutive
green boots after the redesign; `nova test` 21/21.

## Hard lessons

- Blind STI inside an ISR is a nesting hole: the first
  hook's plain cli/sti lock re-enabled interrupts
  mid-handler (before EOI), letting a nested IRQ
  interleave a ring drain (shared `last_used`:
  double-consume/skip + queue slot collision). IF-saving
  locks close it; the tick needs no lock creativity at
  all (it runs IF=0 to completion).
- A drill that doesn't hold its exclusion is a race
  report, not a proof: the first drill's 20-instruction
  bump-then-lock window let a real IRQ steal the fake
  frame (slot filled, return discarded: divert==0 with
  bytes present -- exactly the observed signature).
  Craft and restore live under the same lock as the pump.
- Three different failures in three boots is a
  concurrency signature, not three bugs: stop adding
  instrumentation after the second one and re-examine
  the exclusion design instead. (The ARP/ping timeouts
  were never individually root-caused; they vanished
  with the nesting hole, which is documented here, not
  claimed as proven.)
- `mm_exit`'s blind STI predates this work and stays:
  out of scope, same hazard class, noted for the future.

## Consequences

- All wire RX arrives within a tick of completion, socket
  or legacy, with no ISR ring work and no thread spins.
- Remaining: more sleep callers (getkey/disk); a drill
  for the tick path itself is inherently racy (async
  trigger) and stays unproven by construction -- the pump
  it runs is the drilled one.
