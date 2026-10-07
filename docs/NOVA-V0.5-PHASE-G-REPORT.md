# NOVA v0.5 Phase G Report — Scheduler & Process Observability

## 1. PHASE G STATUS

PASS

## 2. BASELINE

`27c5945` clean; reproduced 23/23, CORPUS-OK 7/7, gate
PASS (TRACEFETCH-OK/ADV-OK, RUNTIME-OK, PERF-OK, lint).

## 3. OBSERVABILITY INVENTORY

SYS_STATS (35, fixed 20B), SYS_RUNTIME (37, fixed 16B),
tid/state/prio/runtime_ticks/slice_used, 32 runqueues,
sleep list, g_current, g_sched_switches, ticks_used,
thread_count, CREATE/EXIT/SLEEP/WAKE trace, shell
stat/ticks/rt, SDK mirrors. Every field traced to its
producer (§4 table in SCHEDULER-OBSERVABILITY.md).

## 4. THREAD STATE MODEL

READY/RUNNING/ZOMBIE/BLOCKED (no SLEEPING; timed bit
distinguishes). Full transition table + illegal
transitions enumerated; accrual possible only in
RUNNING (proven, not asserted).

## 5. CURRENT THREAD AUDIT

`g_current` set at `sched_enter`, unique under single
executor. Tick charges the preempted runnable thread;
no mismatch window exists (IF=0 across tick; switches
only at defined points).

## 6. RUN-QUEUE AUDIT

Runnable count computable but NOT exposed — decision:
no invariant needs it (runtime+state+batteries prove
everything). Same for enumeration: query-by-tid only.

## 7. RUNTIME ACCOUNTING CORRELATION

`proc=140 sum=140` exact; `hog=30 low=10 early=0`;
freeze windows; aggregation invariant holds.

## 8. STRICT-PRIORITY MEASUREMENT

Policy invariant, twice proven (STARVE + hog/low).
`early=0` (L never ran before H parked). Unchanged.

## 9. ROUND-ROBIN MEASUREMENT

A/B prio-16 both [60,62]: rotation observable via
10-tick alternation; ranges (not equality) are the
contract (slice-edge partials). SCHED-W corroborates.

## 10. SLEEP/WAKE MEASUREMENT

Timed + untimed share BLOCKED; park ≤5, 30-tick freeze,
wake latency bound 2 (SCHED-W) + resume test jointly
prove wake→READY→execution→re-accrual.

## 11. BLOCKING MEASUREMENT

Untimed park (A/B/H/L) frozen + resumed; timed sleep
(C) frozen + exited on wake. Same state, both paths.

## 12. PREEMPTION MEASUREMENT

10-tick slices intact (`SCHED_SLICE_TICKS` untouched);
alternation + hog dominance measured, not modified.

## 13. PROCESS/THREAD CONSISTENCY

Exact equality while contributors alive/stored; exited
shares persist in process counter (D: 20 kept);
reaped threads vanish individually by design.

## 14. SYS_RUNTIME AUDIT

EAX=37, (tid, out), 0/`-ENOENT`/`-EFAULT`, 16B fixed,
no pointers/padding/uninit, stable refusals.
Single-executor + cli lookup: no lifecycle race.
Unchanged since Phase F (this phase only re-proves).

## 15. SYS_STATS AUDIT

Byte-identical since Phase E (verified: Phase F/G
added zero lines touching `do_stats`; size/offsets/
number/mirror/shell/selftest confirmed). No growth
(no compatibility strategy needed).

## 16. VISIBILITY/SECURITY

Both calls world-readable (stats precedent); scalars
under cli, never pointers. Thread-existence timing
visible — same posture as WINSTAT. No high-res
surface; side-channel surface unchanged.

## 17. SHELL DIAGNOSTICS

`rt [tid]` verified ticks-labeled (now
`runtime_ticks=`); `stat`/`ticks` unchanged. No
cosmetic additions.

## 18. TRACE CORRELATION

seq=order, tick=IRQ count, runtime=execution. Never
merged. Lifecycle events corroborate; ABI untouched.

## 19. SMP CONSIDERATIONS

Single-executor assumed everywhere (current, queues,
attribution, aggregation). SMP needs per-CPU current +
attribution + aggregate locking. Nothing added now.

## 20. PERFORMANCE

Tick path: +3 insns (Phase F, unchanged). 200 queries:
0 ticks (<10 ms for 200 calls — below resolution,
reported honestly). All PERF deltas zero. No TSC.

## 21. TESTS

Battery A–I mapped: A (D/exiter), B (A/B + SCHED-W
RR), C (H/L + STARVE), D (C + accuracy), E (A/B/H/L
parks), F (NEW: woke-count 5 + monotonic re-accrual),
G (D + ENOENT), H (fresh-process isolation), I
(proc=140 sum=140). Order: unit, corpus, gate, RUNTIME,
SCHED-W, TRACE, ADV, PERF, lint, diff, gate — green.

## 22. BUGS

Product: NONE. Test-draft (mine, pre-boot): scratch
overwritten by later queries before byte-check (fixed:
re-fetch first); release-wake skipped the resume
counter (fixed: count every resumption). Both caught by
the battery doing its job. No baseline behavior
changed at any point.

## 23. DOCUMENTATION

`docs/SCHEDULER-OBSERVABILITY.md` (new, 17 items),
`docs/RUNTIME-ACCOUNTING.md` §15 (resume/query-cost),
`CHANGELOG.md` Phase G entry. No ABI doc changes
needed (no ABI change).

## 24. ADR

No new ADR required: no architectural fork (counts,
visibility, widths all follow established precedent).

## 25. FILES CHANGED

`kernel/sched/sched.c` (resume test + query-cost),
`userspace/init/sh.c` (`runtime_ticks=` label),
`docs/SCHEDULER-OBSERVABILITY.md` (new),
`docs/RUNTIME-ACCOUNTING.md`, `CHANGELOG.md`, this
report.

## 26. FINAL GATE

23/23, CORPUS-OK 7/7, GATES-OK, TRACEFETCH-OK,
TRACEFETCH-ADV-OK, RUNTIME-OK, PERF-OK, lint clean (1
pre-existing tolerated warning), diff-check clean,
`ci/gate.py` PASS.

## 27. COMMIT

Single commit `feat(sched): add scheduler observability`
(hash below), tree clean after.

## 28. PUSH

NOT PUSHED

## 29. LIMITATIONS

§14 of the observability doc (no queue-depth query, no
enumeration, ZOMBIE unqueryable, tid-0 caller alias,
sub-tick slice invisible, test-process struct leak).

## 30. NEXT AUTHORIZED STEP

NEXT AUTHORIZED STEP: Phase H requires separate approval.
