# ADR-0071: Observability seed — SYS_STATS + GATES-OK (Phase G)

Status: accepted (v0.3 Phase G). Date: 2026-10-06.

## Context

v0.3 Phase G asked for structured subsystem status and
diagnostic counters. The gate chain already reports per-
subsystem PASS loudly-or-panics; what was missing was (a)
a formal closing line harnesses can grep without parsing
the whole chain, and (b) any read-only kernel telemetry
at all (uptime, switches, IRQs, threads, memory were all
serial-only or invisible).

## Decision

- **`GATES-OK`** printed once in `pm_main` after the last
  spawn proof (before the idle loop / fault injection).
  Semantics: every selftest above passed, because any
  failure panics first. Machine-greppable SYSTEM READY
  for harnesses and CI; added to both gate lists. In the
  fault image it prints before `INJECT-FAULT` (accurate:
  the system was ready, then deliberately faulted).
- **`SYS_STATS` (35, `SYS_MAX` 35)**: read-only counter
  snapshot `{ticks, switches, irqs, threads, pmm_free}`
  (20B, `nova/types.h`-shared). Snapshot-into-local +
  one `copy_to_user` (never exposes addresses; counters
  only). SDK `nova_stats()` + shell `stat`.
- **Proof** (`stats_selftest`, `STATS-OK`): NULL refuses
  (EFAULT), then real dispatch against a scratch
  addrspace (kernel-low cloned, one US/RW frame, CR3
  lent under cli, restored + destroyed). Deterministic;
  asserts sane values (threads ≥ 1, frames free).
- No tracing/profiling yet (event stream, per-thread
  runtimes, wake latencies stay v0.4+): this phase seeds
  the counters (`g_sched_switches` from Phase D is the
  first family member) and the summary line, nothing
  more. Deliberately small: observability that ships
  untested helps no one.

## Evidence

Guest `STATS-OK` + `GATES-OK`; shell `stat` builds (new
user binary surface, exercised at runtime, not gated —
same standing as other shell builtins); `nova test`
to follow at commit (Phase H gates on it); ABI.md
regenerated (new row 35, T17 freshness holds).

## Hard lessons

- A stats test needs a user page, and boot has no user
  mappings: lending a scratch addrspace's CR3 under cli
  is the honest harness (mirrors what a user thread
  would do). Cheaper than spawning a user probe thread,
  equally real dispatch.
- The gate summary belongs after spawn proofs, not after
  selftests: first placement printed before USER/SMP/
  demo gates existed. "Reaching here" claims must name
  what "here" is.

## Consequences

- Harnesses and future CI gate on `GATES-OK` instead of
  enumerating markers (the marker lists stay as the
  detailed contract).
- Counter family has a home and a pattern; Phase G ends
  here, tracing is future work with its own ADR.
