# ADR-0004: Scheduler — replaceable policy, fair default

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Workloads span CPU-bound, IO-bound, interactive, and high-thread SMP.
No single policy wins everywhere; hard-coding one invites rewrites.

## Decision

Scheduler interface + default policy: preemptive, priority-aware, fair
(with affinity and SMP load-balancing hooks). Policy lives behind the
interface; benchmarks (latency, throughput, fairness, ctx-switch cost)
are checked in from Phase 4.

## Consequences

- Single-core correctness first; SMP balancing gated, never assumed.
- Every scheduler change ships with before/after numbers.
