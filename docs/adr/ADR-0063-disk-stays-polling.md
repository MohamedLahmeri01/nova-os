# ADR-0063: Disk stays polling — IRQ-driven DMA deferred (16-ii)

Status: accepted (assessment). Date: 2026-10-06.

## Context

The sleep arc (15-i..v, 16-i) set out to convert "the last
polling family": disk waits. ATA looked like the last
spin. Reading the driver changed the conclusion: there is
no disk equivalent of the shell's idle poll or the
socket's unbounded wait.

## Decision

- Disk waits stay bounded port-I/O spins. No IRQ support,
  no waiter, no change. The sleep arc is complete without
  them.
- Rationale, point by point:
  - Every ATA wait is already bounded (`ATA_SPIN_MAX`,
    2M polls): typical completions land in microseconds;
    the bound exists for dynamic-VDI first-write stalls
    (seconds worst case, documented) and fails audibly
    (`-EIO`) on dead drives.
  - IRQs are masked by design (`0xC0` on the slave mask,
    "polling never waits on these IRQs, so masking is
    race-free"): there is no completion signal to sleep
    on. Unmasking means IRQ14/15 routing, BMIDE interrupt-
    bit handling, and a completion waiter -- interrupt-
    driven DMA, a phase of its own, on the boot-critical
    path (every FS read, exec, and page-in flows through
    it).
  - No idle case exists: nothing parks on disk. The shell
    parks on keys, sockets park on packets; disk users
    always want exactly one sector, imminently. Sleeping
    would save nothing measurable and risk everything
    load-bearing.
- Audit of every other spin site (same pass): compositor
  demo waits (timed demo, intentional), SMP/PIT boot
  sync (boot-only), selftest watchdogs (hlt, not spin),
  net/sock early-boot spins (pre-scheduler only),
  `rx_wait` (bounded by timeout). No other idle spin.
  The audit, not the conversion, was the deliverable.

## Evidence

Code reading (`kernel/drivers/ata.c` waits + mask +
bound comment); no behavior change, so no gate: suite
re-run green to confirm the tree still passes untouched
(`nova test` 21/21).

## Hard lessons

- "Last X" roadmap items deserve a re-read before
  implementation: the item assumed a spin where a bound
  stands. The assessment overturning the item IS the
  work; implementing anyway would have been motion.
- IRQ-driven DMA stays a real future phase (worth doing
  when disk contention exists: two threads doing FS I/O
  at once). Its design starts from unmasking + BMIDE
  interrupt bits, not from the sleep primitives.

## Consequences

- Sleep callers: sockets, keyboard. Disk: polling, by
  decision, until contention justifies ATA IRQs.
- Nothing to release, nothing to gate.
