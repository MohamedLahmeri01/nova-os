# ADR-0068: Scheduler measurement — starvation proven, last_used audited (Phase D)

Status: accepted (v0.3 Phase D). Date: 2026-10-06.

## Context

The scheduler was strict-priority with starvation
self-documented but unmeasured (ADR-0004's "fair default"
and "benchmarks from Phase 4" both unmet). v0.3 Phase D
measures first, replaces nothing. Same pass audits the
`last_used++`/preemption interaction the v0.2.0 audit
flagged (P1-5).

## Decision

- **Starvation test** (`sched_starve_selftest`,
  `STARVE-OK` gate): prio-31 hog (300M iterations, never
  yields) vs prio-1 worker (100k, exits). The worker's
  first act records whether the hog had finished. Result
  on the constrained VM:
  `STARVE hog_ticks=14 switches=3 early=0 done=1`.
  Reading: the worker ran strictly after the hog
  (starvation demonstrated deterministically by ORDER,
  not timing); exactly 3 switches (main→hog→worker→
  main) although the timer preempted ~14 times — the
  mechanism made visible (preemption fires, rescheduling
  has nowhere else to go). This test is a tripwire: any
  future fair policy must flip `early` to 1.
- **1-CPU vs 2-CPU (§17): identical by construction.**
  APs park (`cli;hlt`, no scheduler); the second CPU
  executes no threads. No separate measurement exists
  because there is nothing to measure — stated, not
  skipped.
- **Switch counter** (`g_sched_switches`, bumped in
  `sched_switch_to` — the single funnel for yield,
  preemption, exit, and wake paths): first latency/
  throughput number in-tree; Phase G grows the family.
- **No policy change.** Strict priority stands until a
  workload justifies aging/classes with its own numbers
  (likely target remains priority+fairness+QoS).
- **`last_used` audit (§19): no change, audit recorded.**
  RX/TX `last_used` advances run under the RX subsystem
  lock (IF=0) or in thread-only TX paths; preemption
  needs an IRQ return, and no IRQ fires mid-hold; a
  pre-set `need_resched` is consumed at the next IRQ,
  i.e. between critical sections, never inside. NMI/SMI
  and AP executors are the only holes, both out of scope
  (no NMI handlers; APs parked). Verdict: sound today,
  re-audit compulsory at first AP-scheduling commit.
- Stale comments fixed as touched (`sched.h`/sched.c
  said round-robin; strict since 4c).

## Evidence

`STARVE hog_ticks=14 switches=3 early=0 done=1` +
`STARVE-OK` on serial; full `nova test` 21/21 (no ABI
change). 1-vs-2-CPU equivalence by code reading
(`ap_entry` parks), not by measurement — labeled as
such above.

## Hard lessons

- At 30M iterations the hog cost 1 tick (uninformative
  granularity): size measurement loops against the tick
  (300M → 14 ticks), not against aesthetics. Sibling
  tests already knew this scale; reuse it.
- `switches=3` is the more eloquent number: it proves
  the timer *did* preempt and the scheduler *declined*
  to switch — starvation as mechanism, not anecdote.

## Consequences

- Fairness work now has a baseline and a tripwire; no
  policy PR merges without moving these numbers and
  updating this test.
- Scheduler tracing (per-thread runtimes, wake latencies
  finer than ticks) waits for Phase G observability.
