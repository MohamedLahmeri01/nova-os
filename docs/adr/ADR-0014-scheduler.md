# ADR-0014: Threads and preemptive round-robin scheduler

Status: accepted (Phase 4a). Date: 2026-10-01.

## Context

Phase 4 needs processes, threads, and scheduling without boiling the
ocean. Single core only (APs parked); user mode and address spaces
come later.

## Decision

Kernel threads on 8KB PMM-contiguous stacks with canary words checked
on every switch-in; minimal process containers (pid + member list,
shared PD for now). Scheduler behind policy ops (add/remove/pick);
default is preemptive round-robin, 100ms slices, driven from IRQ0 via
a need_resched flag consumed in irq_common before iret (plus direct
cli-guarded yield). Exit reaps via a graveyard (never frees a dying
stack under itself). Proof is a tick-window overlap test: a
never-yielding 300M-iteration thread must strictly contain two
shorter threads' windows — impossible without timer preemption.

## Evidence

Guest: `SCHED threads=4`, `SCHED preempt=1`, `SCHED-OK`; `nova test`
7/7 with SCHED-OK in T5/T6 gates.

## Hard lessons

- Preemption tests must not serialize workers behind full slices: an
  early design (yield-every-iteration short threads vs one long thread)
  inverted the timing and could never observe overlap. Overlap of
  recorded tick windows is deterministic; poll-luck is not.
- Runqueue and process lists must not share one link field (caught by
  inspection before it bit).
- Yield/switch paths need cli guards: a timer IRQ between pick and
  switch would resume a stale pick. Resume paths restore/sti
  explicitly.

## Consequences

- Fairness/priority/SMP balancing stay behind the same ops (Phase 4b).
- No locking (single core, documented for SMP).
- Per-process address spaces + user mode next (PD field reserved).
