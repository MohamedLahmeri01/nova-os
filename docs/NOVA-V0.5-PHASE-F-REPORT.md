# NOVA v0.5 Phase F Report — Per-Thread Runtime Accounting

## 1. PHASE F STATUS

PASS

## 2. BASELINE

`fddc4ec` clean; reproduced 23/23, CORPUS-OK 7/7, gate
PASS before any modification.

## 3. EXISTING ACCOUNTING INVENTORY

- `process.ticks_used` (`process/process.h:57`): ++ in
  `sched_tick` (`sched/sched.c:348`) iff `cur->proc != 0.
- `thread.slice_used`: slice countdown vs
  `SCHED_SLICE_TICKS=10` (`sched/sched.h:22`).
- `g_current` (`sched/sched.c:44`), 32 runqueues, sleep
  list, graveyard (one slot, freed on next switch/exit).
- No per-thread runtime existed. No detach on exit
  (zombie stays on `p->threads` until reap frees it).
- `SYS_STATS` (35): fixed 20B counters, no per-thread data
  (extending it breaks its stable layout → new call).

## 4. THREAD LIFECYCLE AUDIT

create/create_user zero the struct explicitly (kmalloc
does not zero) → runtime init placed there + adopt_boot.
Exit → zombie handoff (unlink, counter frozen) → reap
frees. No inheritance path exists.

## 5. RUNTIME SEMANTICS

`thread.runtime_ticks` = delivered IRQ0 ticks while the
thread was `g_current`. Unit: ticks, always labeled.

## 6. ACCOUNTING POINT

`sched_tick`, beside the process increment. At that point
`g_current` is the preempted runnable thread (IF=0,
single executor). IRQ work charges the preempted thread:
same posture as `ticks_used` since 13b, documented.

## 7. PROCESS ACCOUNTING SEMANTICS

Case A confirmed: process CPU execution. Invariant:
`process.ticks_used ==` sum over all threads that ever
ran in it (same increment site; exited shares persist).

## 8. THREAD ACCOUNTING IMPLEMENTATION

u32 field (`thread/thread.h`), 0 at all 3 creation paths,
`cur->runtime_ticks++` in tick (`sched/sched.c`), frozen
by unlink at exit. No policy/slice/timer changes.

## 9. BLOCK/SLEEP BEHAVIOR

Parking removes candidacy → zero accrual. Proven: ≤5
ticks for park path, 30-tick freeze window, timed +
untimed sleep covered.

## 10. EXIT/REAP BEHAVIOR

Frozen at handoff, freed at reap, ENOENT after (graveyard
deliberately unsearched). D-test: stored 20, persisted
in process total, ENOENT via ABI.

## 11. IDLE BEHAVIOR

Boot thread (id 0, prio 0) accrues its own ticks; no
other thread/process is charged for them. Verified by
frozen-worker + exact-aggregation tests.

## 12. PREEMPTION

31-hog vs 1-low: `hog=30 low=10 early=0` — strict prio
observed, L never ran before H parked. Policy untouched.

## 13. SCHEDULER INTERACTION

STARVE-OK, SCHEDW-OK byte-identical behavior (14/3/7/20
windows intact). Only added instruction: one inc/store
in tick.

## 14. PROCESS/THREAD AGGREGATION

Fresh process: `RUNTIME proc=140 sum=140` — EXACT u32
equality (60+60+0+20) with all contributors alive or
persisted. No fabrication: shares verified individually
before summing.

## 15. STATS ABI

New `SYS_RUNTIME` (37), not a stats extension (layout
stability). `(tid, out)` → 0/`-ENOENT`/`-EFAULT`; 16B
fixed struct with asserts; tid 0 = caller; `nova_runtime`
wrapper; shell `rt [tid]`. Justification recorded here
per §17 (no existing per-thread surface; fixed-layout
extension would break ABI v1 consumers).

## 16. USER MEMORY SAFETY

Whole-range validate + exact 16B copy (TRACEFETCH
discipline). Tested: NULL/RO/kernel/wrap/cross-page →
EFAULT with canary proof; unknown/exited → ENOENT;
exact-byte write proof. No len arg → undersized N/A.

## 17. SECURITY

World-readable, stats precedent; counters only (no
pointers/regs/stacks). Lookup snapshots scalars under
cli, never returns pointers (no UAF). Thread existence
visible — same posture as WINSTAT/process enumeration.

## 18. TRACE INTERACTION

No trace changes. CREATE/EXIT/SLEEP/WAKE corroborate
lifecycle; no instrumentation added.

## 19. COUNTER WIDTH/OVERFLOW

u32 (matches `g_ticks`/`ticks_used`): 497.1-day horizon,
monotonic, mod-2^32. No signed runtime arithmetic.

## 20. SMP CONSIDERATIONS

BSP-only (same as scheduler). SMP needs per-CPU
attribution; field layout unchanged, increment site
moves. No per-CPU structures added.

## 21. TESTS

`sched_runtime_selftest` (`RUNTIME-OK`): D persistence,
60-tick RR pair, sleep-charge cap, 30-tick freeze, exact
aggregation, 31-vs-1 preemption, 12-vector ABI battery,
worker release with reap accounting. All watchdogs
tick-bounded (8000).

## 22. PERFORMANCE

+3 instructions (load/inc/store) on the tick path, no
branches/alloc/locks. PERF deltas all zero
(ctx/heap/lock/sock/fs/copy/trace/thread/call
unchanged). Bench methodology untouched (no TSC).

## 23. BUGS FOUND/FIXED

Product: NONE (one test-draft ebx/ecx mixup caught on
review pre-boot; one missing-gate-marker caught by list
inspection). No baseline behavior changed.

## 24. DOCUMENTATION

`docs/RUNTIME-ACCOUNTING.md` (new, 15 items),
`docs/SYSCALL-ARGUMENTS.md` row 37, `docs/ABI.md` regen
(row 37, MAX 37), `CHANGELOG.md` Phase F entry.

## 25. ADR

No new ADR required (§27): u32 consistent with existing
conventions; visibility follows stats precedent; no
architectural fork.

## 26. FILES CHANGED

`kernel/thread/thread.{h,c}`, `kernel/sched/sched.{c,h}`,
`kernel/syscall/syscall.{c,h}`, `sdk/include/nova/
{nova.h,types.h}`, `userspace/init/sh.c`,
`kernel/pm_main.c`, `tools/nova.py`, `docs/ABI.md`,
`docs/SYSCALL-ARGUMENTS.md`, `CHANGELOG.md`,
`docs/RUNTIME-ACCOUNTING.md`, this report.

## 27. FINAL GATE

23/23, CORPUS-OK 7/7, GATES-OK, TRACEFETCH-OK,
TRACEFETCH-ADV-OK, RUNTIME-OK, PERF-OK, lint clean (1
pre-existing tolerated warning), diff-check clean,
`ci/gate.py` PASS.

## 28. COMMIT

Single Phase F commit (hash below), tree clean after.

## 29. PUSH

NOT PUSHED

## 30. NEXT AUTHORIZED STEP

NEXT AUTHORIZED STEP: Phase G requires separate approval.
