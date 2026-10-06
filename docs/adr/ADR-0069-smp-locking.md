# ADR-0069: SMP locking model — primitives now, scheduling later (Phase E)

Status: accepted (v0.3 Phase E). Date: 2026-10-06.

## Context

SMP state was bring-up only (MP parse, AP park) with zero
locking anywhere: heap/PMM/runqueue/FS/ring code all
carried "SMP adds it" comments, and the 2-CPU test VM
invited false confidence. v0.3 Phase E lays foundations
without enabling AP scheduling (§25: APs stay parked).

## Decision

- **Arch atomics** (`arch_atomic_xchg/cmpxchg/add_u32`,
  `arch_pause`, both backends): LOCK-prefixed, bus-correct
  now. Generic code never inlines LOCK asm.
- **`struct spinlock`** (`kernel/sync/`, fills the empty
  dir): test-and-set + pause, trylock/EBUSY, owner token
  (diagnostic), irqsave pair with IF-saving restore (the
  blind-`sti`-in-handler lesson from ADR-0061 applied
  preemptively). Non-reentrant by design; sleep forbidden
  while held. No dependencies beyond arch + headers, so
  the contract proofs run on host.
- **Locking policy** (`docs/LOCKING.md`, normative):
  per-structure table (guard today / IRQ stance / sleep
  rule / SMP future), no-nesting rule, leaf-first ordering
  for later, and the AP-scheduling checklist (locks +
  per-CPU + TLB shootdown + re-audit).
- **Per-CPU design**: array/segment options recorded;
  implemented only `smp_cpu_id()` (LAPIC ID register,
  hardware truth) + MP-table BSP record, asserted in
  `smp_boot` (`SMP-CPU id=0`, stable + matches table).
  Full `per_cpu()` waits for an AP executor to serve.
- **Proofs**: host T22 (atomics vectors, lock/try/owner
  contract; irqsave + contention guest-only) and guest
  contention (two threads × 100k irqsave increments,
  exact 200k or failure, `SYNC-OK` gate). APs parked,
  unchanged.

## Evidence

Host `VERDICT: sync correct`; guest `SYNC-OK` +
`SMP-CPU id=0`; full `nova test` 22/22 (T22 new; no ABI
change).

## Hard lessons

- The empty `kernel/sync/` dir turned out to be a correct
  prediction: filling a planned home beats creating a new
  one. The other ten empty dirs get the same treatment
  in Phase F (fill with intent or delete).
- Owner-as-diagnostic (not enforcement) keeps the
  primitive dependency-free and host-testable; enforcement
  would have dragged in the scheduler and split the proof.

## Consequences

- First lockable code exists; nothing is locked yet that
  wasn't IF-guarded before (net RX lock predates this).
  Adopting spinlocks per structure is follow-up work,
  each with the §24 stress matrix (atomics, IRQ
  interaction, allocator, refcounts, wait queues).
- AP scheduling remains a later milestone with its own
  ADR; this one only makes it possible.
