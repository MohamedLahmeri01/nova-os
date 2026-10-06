# NOVA v0.5 Baseline Audit (read-only — no source modified)

Date: 2026-10-06. Method: git inspection + full suite run +
targeted source re-reads. Verdict at the end.

## Tree / version / tests

```text
HEAD:     1df490c (docs: correct v0.4 baseline commit reference)
status:   clean. describe: v0.2.0-31-g1df490c. VERSION: 0.2.0.
tags:     v0.1.0, v0.2.0 (no v0.3/v0.4 tags — correct, undeclared).
suite:    23 passed, 0 failed, 0 skipped (T1–T23 + CORPUS-OK 7/7).
env:      gcc 15.2.0 / Python 3.11.2 / VBox 7.2.6 / Ryzen 5 5500 /
          VM 2 CPU + 1024 MB.
```

## v0.4 claims verified (spot-checked, all hold)

LICENSE present; ADR-0064..0071 present and contiguous
(lint: 0001..0075); copy helpers 8 call sites + stats;
RO text + CPU-NX print + MEMMAP-OK; STARVE/SCHEDW/SYNC/
TRACE/PERF/STATS/GATES-OK gates; corpus dirs
(dhcp/dns/udp/fat32/ext4/nova_fs/shell) replayed;
gate.py PASS/PARTIAL paths; WARNINGS.md matches lint
(1 tolerated).

## Subsystem states (implemented / tested / measured?)

- Trace: ring 256×12B, kinds 1–7, IF-save emit, panic
  tail dump, TRACE-OK selftest. MISSING: cpu_id, pid,
  sequence numbers in records; no fetch ABI; no reader
  except panic + selftest.
- Benchmarks: 8 PERF workloads, tick granularity (zeros
  are <10 ms upper bounds). No TSC anywhere in tree
  (grep clean). No per-op latency below one tick.
- Scheduler: strict-prio + RR/level, 10-tick slices,
  no aging; STARVE tripwire + SCHED-W battery green.
  Measured: overlap, scale, wake ≤2, sleep exact.
- Timing: PIT 100 Hz only (`g_ticks` u32, wrap-safe
  comparisons). No monotonic abstraction, no TSC, no
  calibration, no ns/µs source.
- Threads: no per-thread runtime/switch/sleep counters
  (only `slice_used` + proc-level `ticks_used`).
  `thread_count` exact (reap-and-compare asserted).
- Processes: pid/cr3/fds/caps/uid/gid/jail/rlimits —
  no stats surface beyond SYS_STATS counters.
- FS locking: NONE (no spinlock/IF-guard in fs/;
  single-executor assumption). Refcounts + EBUSY holds
  tested single-threaded.
- Net bounds: MTU 1500, RXQ 4×1600 drop-newest,
  SOCK_MAX 8, payload 1472, demux no-truncate. No TCP.
- Shell: 128B lines, printable-only, per-command bounded
  parsers, no quotes/pipes/redirect; T23 + 16 corpus
  lines + 3k soup green.
- CI: `ci/gate.py` (env+lint+build+image, VBox-aware
  SKIP); no YAML, no runner.
- Warnings: 1 tolerated guest (jail frame) + host-only
  classes documented; no `-Werror`.
- SMP: parked APs, spinlock + policy + cpu_id + PERCPU
  design doc. No per-CPU data, no AP scheduling.

## v0.4 report + ADR-0072..0075

Read. Claims match source (see table above). No
corrections required.

## Claims requiring correction: NONE.

## Planned v0.5 changes (scoped by prompt order)

A: re-triage W1–W6. B–F: trace audit/fetch/ABI/safety/
perf. G–I: timebase/TSC/monotonic. J–L: per-thread
runtime + stats ABI + scheduler observability. M–N:
workloads + policy retain. O–P: FS stress + locking
audit. Q–R: packet corpus + limits. S–T: grammar +
fuzzing. U–V: diagnostic tool + benchmarks. W–Y:
warnings/format/CI. Z: SMP design doc + ADR.

## Verdict

```text
BASELINE VERIFIED
23/23
TREE CLEAN
```

Cleared for v0.5 Phase A implementation.
