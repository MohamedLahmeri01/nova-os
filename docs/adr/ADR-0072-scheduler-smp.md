# ADR-0072: Scheduler workloads + policy retained + SMP audit (v0.4 E–I)

Status: accepted (v0.4). Date: 2026-10-06.

## Context

v0.4 demanded scheduler measurement (workloads, starvation,
accounting), an SMP-foundation audit, and a per-CPU design —
without changing policy or enabling AP scheduling. v0.3 had
left exactly one measurement (STARVE hog+worker) and the
foundations (atomics, spinlock, LOCKING.md, cpu_id).

## Decision

- **Workload battery** (`sched_workload_selftest`,
  `SCHEDW-OK`): RR-equal pair (overlap proves slice
  rotation: `overlap=1 switches=3`), six-runnable scale
  (`n=6 switches=7` — six workers plus the return home,
  minimal and exact), wake latency (1 tick vs bound 2),
  sleep accuracy (asked 20, actual 20 — tick-exact
  expiry). Machine-readable `SCHED-W` lines on serial
  (guests cannot hand files to the host; no FS writes —
  selftests leave pools clean). Omitted with reason:
  IO_PERIODIC (no periodic-I/O source exists),
  run-queue occupancy (any observer thread perturbs;
  waits for Phase G tracing).
- **Starvation analysis (§F): three levels implied by one.**
  STARVE proves strict containment (worker runs only
  after the hog); the mechanism (preemption fires,
  rescheduling has nowhere else to go) generalizes to N
  levels trivially — a med thread would slot between hog
  and worker by the same pick rule. No separate med test:
  same code path, no new information.
- **Policy retained: strict priority.** Reason: all
  measured behavior is correct-by-design (rotation
  within level, preemption across levels, bounded wake/
  sleep); no workload demonstrates a need for aging or
  classes. Known starvation semantics: any runnable
  higher-priority compute loop starves everything below
  it, indefinitely; the STARVE tripwire pins this so a
  future policy change must update the test, not sneak
  past it. Future mitigation (aging or QoS classes) needs
  workload numbers first — which now exist to compare
  against.
- **Accounting re-audit (§G):** `ticks_used` ticks once
  per timer IRQ on the running proc (single writer,
  IRQ-only — sound); `g_sched_switches` bumps in the
  single switch funnel (all paths covered); thread
  birth/death via count + graveyard (lifecycle proven by
  the new reap-and-compare assert in STARVE:
  `thread_count` returns to base); sleep/wake state in
  one list with one waker discipline; no per-thread
  runtime yet (ticks are per-process — noted gap, not
  added: no consumer exists). Counters are u32 with
  wrap-safe comparisons; absolute values meaningless
  across ~497-day wrap (accepted, documented).
- **SMP audit (§H):** LOCKING.md hardened normative
  (derived nesting — three sites, all provably
  non-contending; sleep/scheduler-lock bans; IRQ rules;
  refcount rules). No code change: the audit found the
  discipline already held (single executor + IF rules).
- **Per-CPU (§I):** `docs/PERCPU.md` — structure,
  accessor candidates (LAPIC-indexed array preferred),
  migration checklist, anti-patterns. No code: no AP
  executor exists to serve.
- **irqsave assert fixed in passing:** first version
  compared the saved IF word against 1; the word is the
  bit (0x200). The failure was the test's, caught by the
  test — noted so the pattern (`!= 0` vs `== 1`) is
  reviewed everywhere IF words are compared.

## Evidence

`SCHED-W rr overlap=1 switches=3` / `many n=6
switches=7` / `wake latency=1 bound=2` / `sleep asked=20
actual=20` + `SCHEDW-OK`; lifecycle count returns to
base; full suite at commit (Phase U gates on it).

## Hard lessons

- switch counts are more eloquent than tick counts:
  `switches=3` for two workers proves zero spurious
  switches; `switches=7` for six proves linear handoff.
  Count transitions, not just time.
- A same-level pair that never yields still interleaves
  (slice rotation in pick) — RR-within-level is real,
  not just documented. The overlap assert is the proof;
  totals alone would not distinguish it from FIFO.

## Consequences

- Scheduler policy work now needs: a workload showing
  harm + updated tripwires + this ADR superseded. The
  bar is explicit and high on purpose.
- Tracing (per-thread runtimes, sub-tick latencies)
  stays future work with its own ADR.
