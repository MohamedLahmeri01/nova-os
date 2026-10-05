# ADR-0052: Virtio interrupts — fielded, counted, proven (14c)

Status: accepted (Phase 14c). Date: 2026-10-06.

## Context

Used-rings were polled; the device could signal all along and
we never listened. Full blocking RX needs wait queues
(scheduler work, later) -- but fielding + proving interrupts
is separable, committable, and prerequisite to all of it.

## Decision

- `virtio_net_init` reads PCI Line/Pin (here: line 9, pin 1),
  logs `VIRTIO-IRQ`, and unmasks that PIC bit read-modify-write
  (timer/kbd/dormant-mouse masks preserved). Unroutable lines
  stay polling-only (counter assert skips).
- `virtio_net_irq` (IDT vec 32+line, dispatched in `irq_handler`
  past the fixed vecs): kernel-CR3 window, ISR read acks
  level-triggered INTx before the generic EOI (no storm),
  counts nonzero ISR only (shared-line strangers don't count).
- `net_selftest` asserts counter > 0 after wire traffic (ARP +
  ping + DHCP + DNS TX/RX completions). Polling paths
  untouched (mechanism unchanged by design).

## Evidence

Guest counter 5 pre-probe, 6 after one forced completion;
`NET-WIRE-OK`; `nova test` 21/21.

## Hard lessons

- The masked cascade deafens the whole slave PIC: master
  0xFC opens IRQ0+1 but blocks IRQ2, so IRQ8-15 can never
  arrive regardless of the slave mask. Slave devices need
  master 0xF8. This also explains why the (dormant) mouse
  could never have interrupted -- same root cause, found by
  the virtio work, not the mouse work.
- Isolate delivery-vs-device in one boot: sample ISR + masks
  + count before and after a forced completion with no
  polling in between. `pre=00fdfc00, post=00fdfc01` read as:
  line open, device signaling (ISR latched), nothing
  delivered -- pointing straight at the PIC, not the device.
- TEMP-DIAG leaves no trace: the probe block, its accessor,
  and its decl were all removed post-proof (grep-verified).

## Consequences

- Polling remains the mechanism; blocking RX (wait queues +
  sleep/wakeup + timeouts) is the designed next step.
- Next: widgets assessment, then release work.
