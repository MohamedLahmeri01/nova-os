# Proposed ADR — SMP strategy: design-first, BSP-only until M2 (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

APs parked (ADR-0069); nearly every v0.5 argument
(BSP-only runqueues, cli-sections, park discipline,
THREX pool math) assumes one executor. Options:
(a) full SMP with per-CPU data + runqueue/sleep
locks + TLB shootdown (LOCKING.md checklist);
(b) asymmetric: control CPU + RT CPU split;
(c) stay BSP-only forever (reject for infra: caps
throughput and credibility).

## Trade-offs

(a) is the only general answer; it invalidates all
BSP-only reasoning at once, hence the M2 re-audit
gate. (b) is cheaper and fits D (deterministic
profile) but complicates the product story; keep
as a fallback if (a)'s re-audit fails.

## Prerequisites / evidence to decide

M1 lock/IF contracts; contention measurements on
2+ vCPUs BEFORE choosing lock granularity
(SYNC-OK pattern extended); TLB-shootdown design.
No AP code until the design passes review.
