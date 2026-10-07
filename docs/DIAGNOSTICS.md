# NOVA Diagnostics (v0.5 Phase K — contract, not a framework)

Model: structured trace + bounded diagnostics +
counters/statistics + deterministic tests. No logging
explosion, no full-packet/operation dumps.

## 1. Architecture

Surfaces (producer → consumer): trace ring (kernel →
panic/selftests/fetch) · runtime stats (tick →
SYS_RUNTIME) · scheduler obs (tests) · panic/fault
(serial) · shell errno texts (user) · FS errno+fsck
(user) · net errno+gates (user) · self-test FAIL lines
(serial) → harness markers. §15 maps each surface.

## 2. Trace relationship

Ring: 256×16B, overwrite-oldest, per-emit seq, tick
stamps (IRQ count, not wall time). Panic dumps last
12 with seqs. Users fetch projected 20B records
(newest 256, loss visible as seq gaps). Ordering
authority is seq, never tick.

## 3. Runtime statistics

Per-thread delivered ticks + process totals
(`docs/RUNTIME-ACCOUNTING.md`); `SYS_RUNTIME`
(tid/runtime/state/prio) and `SYS_STATS` (counters).
Cumulative measurements, not events. do_stats is a
fuzzy snapshot ( unlocked counter reads); runtime
query is cli-atomic.

## 4. Scheduler observability

States, RR/strict-prio batteries, wake-latency and
sleep-accuracy lines (`docs/SCHEDULER-OBSERVABILITY.md`).
No policy exposure beyond behavior proofs.

## 5. Panic/fault reporting

Panic (serial-only): reason, cpu, ticks, tid/pid (IDs
only, -1 when none), regs, CR0/CR2/CR3, guarded stack
walk (≤8 frames), trace tail. Traps: vec+name, err,
tick, regs, PF class (U/S·R/W·P/N). Full dumps are
physical-console privilege, never user-visible.

## 6. Error-code model

Linux-compatible numbers, negative on return. ERROR =
recoverable op failure (caller handles); FAULT =
exception (trap → panic; no recovery in v0.5);
PANIC = halt with reason+evidence. Self-test `-1`
means FAIL-tagged assertion (convention, not errno).
Shell prints `E<num>` (denials keep friendlier text).

## 7. Filesystem diagnostics

Errno per op (fail-closed order), fsck verdicts,
refs/mode asserts in stress, FAIL-vector tags.
Legitimate failure vs corruption distinguished by:
errno path (no structural claims) vs fsck/tree
validators (structural claims).

## 8. Network diagnostics

Errno per op (EAGAIN/ETIMEDOUT/EMFILE/EBUSY/EBADF),
queue/pool bounds observable via stress lines,
corpus expectations per seed. No packet dumps:
protocol class + error class + seed reproduce.

## 9. Shell diagnostics

Usage/unknown/denied/`E<num>` texts, all bounded
(<4KB/line); state resets every line (locals only).
See `docs/SHELL-ROBUSTNESS.md`.

## 10. Resource limits

Trace 4KB ring · panic ≤8 frames + 12 tail records ·
shell <4KB/line, zero heap · serial = host file
(bounded by run) · self-test stacks ≤1KB buffers ·
no diagnostic allocator exists (nothing to leak).

## 11. Security/disclosure policy

User-visible: counters, ids, errno numbers, projected
trace fields, validated packet metadata. Never
user-visible: kernel pointers, CR3/SP/regs, page-table
addresses, credentials, raw CR2, uninitialized bytes,
untrusted packet contents beyond validated fields.
Serial dumps (regs/CR2/CR3) are physical-console-only
by policy, not by accident.

## 12. Concurrency rules

Trace emit IF-save (any context); fetch snapshot IF,
copy after; panic runs cli'd; stats fuzzy-ok, runtime
cli-exact; FS/net locks never held across sleep;
diagnostics take no locks except the subsystem lock
they report under (no new lock order: rx→sock only).

## 13. Failure-injection policy

No permanent hooks. Natural exhaustion covers
ENOSPC/EMFILE/EAGAIN paths (proven in stress);
OOM injection unavailable (accepted: arena too large
to exhaust deterministically in-guest). Host fuzz
covers parser refusals.

## 14. Reproducibility procedure

Scenario + seed/input + trace tail (ticks+seqs) +
runtime evidence + FAIL tag = reproduce. Exact
re-execution (record/replay) explicitly out of scope.
UD-fault path (T6) is the reference correlated
example: marker + vec + tail + halt.

## 15. Limitations & future

No fault recovery (panic always); no double-fault
handler (triple-fault resets); PF-only trace emission
(UD-class faults correlate via tick only); fuzzy
stats snapshot; no wall clock; no packet/operation
tracing. Each is accepted, none blocks diagnosis.
