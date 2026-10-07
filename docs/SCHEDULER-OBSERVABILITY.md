# NOVA Scheduler Observability (v0.5 Phase G — audit + measurement)

Policy unchanged. This phase measures; it does not steer.

## 1. State model

Four states (`thread/thread.h`); no SLEEPING — timed and
untimed sleep share BLOCKED, distinguished by
`sleep_timed`:

| State | Runs? | Accrues? | On runqueue? | Selectable? |
|---|---|---|---|---|
| READY 0 | no | no | yes | yes |
| RUNNING 1 | yes (is `g_current`) | yes | no | — (already) |
| ZOMBIE 2 | no | no | no | no |
| BLOCKED 3 | no | no | no (sleep list) | no |

Transitions: READY→RUNNING (pick), RUNNING→READY
(preempt/yield), RUNNING→BLOCKED (sleep), BLOCKED→READY
(wake), RUNNING→ZOMBIE (exit), ZOMBIE→freed (reap).
Illegal and absent: READY→BLOCKED, BLOCKED→RUNNING
direct, any accrual outside RUNNING, any post-ZOMBIE
accrual (all proven by `RUNTIME-OK` battery).

## 2. Thread observables (authoritative?)

| Field | Source | Unit | Authoritative |
|---|---|---|---|
| tid (`id`) | creation counter | — | yes (unique, never reused) |
| state | scheduler-owned | enum | yes at read instant |
| prio | creation arg (clamped 0..31) | level | yes (immutable) |
| `runtime_ticks` | `sched_tick` iff current | delivered ticks | yes (monotonic, frozen when not RUNNING) |
| `slice_used` | `sched_tick` countdown | ticks of 10 | internal (reset on expiry; not exposed) |

## 3. Runtime semantics

See `docs/RUNTIME-ACCOUNTING.md`. Tick = delivered IRQ0
while current (ADR-0078). IRQ work charges the preempted
thread; sub-tick invisible; u32/497-day horizon.

## 4. Scheduler observables

`g_current` (the RUNNING thread), 32 runqueues, sleep
list, `g_sched_switches`, `STARVE`/`SCHED-W` lines.
Runnable count is computable by walking queues but NOT
exposed: no invariant needs it (§7 decision: unnecessary,
not added).

## 5. Process observables

pid, member list, `ticks_used` (== sum over all threads
that ever ran, proven `proc=140 sum=140`),
`thread_count()`, rlimits/caps/uid. No enumeration ABI
(query by tid only — §7 decision).

## 6. `SYS_RUNTIME` (37)

`(tid, out)` → 0/`-ENOENT`/`-EFAULT`. 16B struct
(tid/runtime/state/prio, asserts hold), tid 0 = caller,
world-readable, whole-range validated, exact-byte copy.
Re-audited in Phase G: number, table row, SDK mirror,
handler, and all refusal paths unchanged since Phase F.

## 7. `SYS_STATS` (35)

Unchanged by Phases F/G (verified: no added lines touch
`do_stats`; struct 20B, offsets, number, mirror, shell
`stat`, selftest all byte-identical). Compatibility
strategy stands: fixed ABIs never grow; new data gets
new narrow calls.

## 8. Shell diagnostics

`rt [tid]` prints `tid= runtime_ticks= state= prio=`
(units explicit; never ms/%). `stat`/`ticks` unchanged.

## 9. Strict-priority measurement

Policy invariant, proven twice: STARVE hog-contains-
worker + Phase F/G `hog=30 low=10 early=0` (L never ran
before H parked; exact lower bounds, +2 park margin).

## 10. Round-robin measurement

A/B same prio-16: both land [60,62] for a 60-gate —
rotation observable via 10-tick slice alternation
(`SCHED-W rr overlap=1 switches=3` corroborates).
Exact equality not promised (slice-edge partials);
ranges are the contract.

## 11. Sleep/block behavior

Timed (`sleep_until`) and untimed (`sched_sleep`) share
BLOCKED: park path ≤5 ticks, 30-tick freeze windows for
both, resumption proven (`resumed+woke`, woke count 5,
monotonic re-accrual). Wake→READY→execution measured by
SCHED-W latency (bound 2) + resume test jointly.

## 12. Aggregation

`process ==` retained-thread sum, exactly, while
contributors alive or stored; exited shares persist in
the process counter (D-test). Reaped threads vanish
individually — by design, no history subsystem.

## 13. Visibility/security

`sched_thread_stat` snapshots scalars under cli, never
returns pointers (no UAF). Both stats calls
world-readable (stats precedent); thread existence and
block timing visible — same posture as WINSTAT/process
enumeration. No high-res surface added (timing
side-channel surface unchanged).

## 14. Limitations

- Runnable/queue-depth query: absent by decision.
- Thread enumeration: absent (tid must be known).
- ZOMBIE unqueryable (ENOENT once exited).
- Boot tid 0 reachable only as caller (0 means self).
- Sub-tick slice position: not observable (by design).
- `rt_p` test process struct leaks (no process reaping
  anywhere yet — pre-existing posture).

## 15. SMP implications

Single-executor throughout: `g_current` unique,
runqueue walk lock-free under cli, attribution exact.
SMP needs per-CPU current + attribution + aggregate
locking; field layouts survive, increment/lookup sites
do not. Nothing added now.

## 16. Trace relation

`seq` = order, `tick` = IRQ count, `runtime_ticks` =
execution. Three concepts, never merged. CREATE/EXIT/
SLEEP/WAKE corroborate lifecycle; trace ABI untouched.

## 17. `g_ticks` relation

Runtime ticks are delivered ticks per thread (ADR-0078
applies: not wall time, minimum-wait direction,
wrap-safe). 200-query cost: 0 ticks (<10 ms for 200
calls — below timer resolution, reported honestly).
