# ADR-0074: Tracing + benchmarks — bounded ring, tick-granular PERF (v0.4 O–P)

Status: accepted (v0.4). Date: 2026-10-06.

## Context

v0.4 Phase O asked for lightweight instrumentation and
Phase P for reproducible microbenchmarks. Constraints:
bounded memory, IRQ-context safety, deterministic tests,
no new ABI surface beyond what exists, no tracing
framework bloat.

## Decision

- **Event ring** (`kernel/trace/`, 256×12B = 3KB .bss):
  `{tick, kind, id, arg}`, kinds CREATE/EXIT/SLEEP/WAKE/
  FAULT/RXDIV/MARK (stable numbers). Emit is IF-saved
  (`arch_irq_save/restore` — the Phase E abstraction pays
  off immediately), so thread and IRQ contexts share one
  path; reserve-then-fill keeps slots untorn (an IRQ
  between reserve and fill takes its own next slot).
  Overwrite-oldest; readers tolerate gaps (selftest
  asserts marker *subsequences*, never exact streams).
- **Emit points** (one line each, no logic moved):
  thread create/exit, sleep park, sleep wake (+timedout),
  page fault (+CR2), socket divert (+port). Deliberately
  NOT traced: context switches and syscalls (too hot —
  the ring would hold ~2 s of timer ticks; counters
  cover them), MMIO register traffic.
- **Panic dump**: last 12 events after `END-PANIC-HALT`
  (marker compatibility preserved: T6 pattern-matches
  the earlier line). Proven in the fault image on the
  first run (thread lifecycle across user/spawn tests
  visible with ticks).
- **Selftest** (`TRACE-OK`): reset/count discipline,
  NULL/empty refusals, marker order, 300-mark overwrite
  wrap (count caps at 256, newest found). No threads
  needed — pure ring mechanics.
- **Benchmarks** (`kernel/bench/`, `PERF-OK`): 8
  workloads sized against the 10 ms tick (loops grown
  until informative or bounded-cheap): ctx ping-pong,
  heap churn, uncontended locks, loopback sockets,
  ramfs file churn (create/write/read/unlink × 20),
  user copies via lent-CR3 mapping, thread churn,
  dispatch floor. Observed (constrained VM):
  `ctx 4000/10, heap 200000/0, lock 4000000/2,
  sock 40000/0, fs 80/0, copy 2000/0, thread 100/0,
  call 1000000/4`. Zeros are upper bounds (< 1 tick),
  not failures — stated so future deltas compare
  honestly. Sleep/wake latency already lives in SCHED-W
  (not duplicated); DNS needs live upstream (skipped
  with reason, DNS-NOLIVE precedent).
- **Finer measurement declined for v0.4**: sub-tick
  precision needs TSC calibration against the PIT
  (~20 lines + error analysis). Recorded as the
  explicit next step for Phase P followers, not
  implemented — tick granularity answers every question
  asked this phase.

## Evidence

`TRACE-OK` + `PERF-OK` in guest; fault-image dump shows
12 events with kinds/ids/ticks; `nova test` at commit
(Phase U gates on it).

## Hard lessons

- The panic dump worked first try *because* the ring
  never depends on the thing that panicked (no heap,
  no locks beyond IF-save, no scheduler state beyond
  g_ticks). Crash tooling must be dependency-minimal —
  this one has exactly one external (ticks).
- Benchmark loops must be sized against the clock, not
  aesthetics: first pass measured mostly zeros (all
  sub-tick). Scale until nonzero or document the upper
  bound — never present a zero as a rate.

## Consequences

- Crash reports now end with forensic context; waiter
  and scheduler bugs gain a flight recorder.
- `nova trace`-style live tooling waits for a demand
  channel design (shell needs a fetch syscall); the
  buffer and reader API are ready for it.
- Benchmark deltas compare relatively across runs on
  the same VM; cross-machine comparison is meaningless
  (ticks are wall-clock here, work-clock nowhere).
