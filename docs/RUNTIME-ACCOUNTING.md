# NOVA Runtime Accounting (v0.5 Phase F)

One counter per thread, one tick source, no new timing.

## 1. Semantics

`thread.runtime_ticks` = number of delivered scheduler
ticks during which the thread was `g_current` when the
IRQ0 handler ran. Unit is always **ticks** (never ms/s).

## 2. Units

Delivered PIT ticks (~10 ms nominal; see `docs/TIMEBASE.md`).
No conversion exists in kernel accounting.

## 3. Source

`sched_tick()` (IRQ0 context): `cur->runtime_ticks++`
beside `cur->proc->ticks_used++`. Three machine
instructions, no branches, no alloc, no locks beyond the
IRQ discipline already held.

## 4. Scheduler interaction

Strict-prio + RR + 10-tick slices unchanged. Accounting
observes the policy: whoever is current at the tick owns
it. Preemption, yield, wake placement all untouched.

## 5. Blocked/sleep behavior

Parking removes the thread from `g_current` candidacy, so
no tick can accrue while BLOCKED (timed or untimed).
Proven: frozen-counter + 30-tick window test.

## 6. Exit behavior

`thread_zombie_handoff` unlinks without touching the
counter: runtime freezes at its final value, then the
memory is freed on reap. Exited threads query as ENOENT
(graveyard deliberately not searched).

## 7. Idle behavior

The boot thread (id 0, prio 0) is the idle fallback and
accrues its own ticks like any thread; other threads are
never charged for time they did not run. Idle does not
inflate any ordinary thread/process counter.

## 8. Overflow

u32, consistent with `g_ticks`/`ticks_used`: 497.1 days
at nominal rate, monotonic, wraps mod 2^32. No signed
arithmetic on cumulative runtime anywhere.

## 9. Aggregation

Same increment site for thread and process counters, so
`process.ticks_used ==` sum over all threads that ever ran
in it — including exited ones (their share persists in
the process counter). Observable equality holds while the
contributors are alive or stored; proven exactly by the
fresh-process test (`RUNTIME proc= sum=` line).

## 10. Visibility

`sched_thread_stat` snapshots scalars under cli (never a
pointer: no UAF surface). `SYS_RUNTIME` is world-readable,
stats precedent: counters only (tid/runtime/state/prio),
no pointers/registers/stacks. tid 0 always means caller
(boot thread id 0 reachable only as caller). New threads
are immediately queryable; dead threads are ENOENT.

## 11. ABI

`SYS_RUNTIME` 37: `(tid, out)` → 0 / `-ENOENT` /
`-EFAULT`. Fixed 16B struct, whole-range validation,
exact-byte copy (TRACEFETCH discipline). `nova_runtime`
SDK wrapper; shell `rt [tid]`.

## 12. Limitations (documented, not fixed)

- IRQ execution charges the preempted thread (same as
  `ticks_used` since 13b; unattributable at tick grain).
- A context switch without a tick moves no accounting
  (sub-tick invisible by design; TSC deferred, ADR-0078).
- Runtime-gated workers accrue +0..2 park-path ticks
  (asserted as ranges, never exact).
- No thread enumeration ABI (query by tid only).
- Reaped threads vanish; only the process total remembers.
- `rt_p` test process leaks one small struct (no process
  reaping exists anywhere yet — pre-existing posture).

## 13. SMP implications

BSP/single-executor assumption (same as the scheduler).
SMP era needs per-CPU attribution or scheduler-owned
counters; the field layout needs no change, only the
increment site.

## 14. Relation to `g_ticks`

Runtime ticks ARE delivered ticks viewed per thread.
Phase E semantics apply unchanged (ADR-0078): not wall
time, minimum-wait direction, wrap-safe comparisons.

## 15. Tests

`sched_runtime_selftest` (`RUNTIME-OK`): exit
persistence, 60-tick spinners, sleep-charge cap, 30-tick
freeze, exact aggregation, 31-vs-1 preemption, full ABI
adversarial (NULL/RO/kernel/wrap/cross/unknown/exited/
exact-bytes), wake-resume proof (Phase G: woke count +
monotonic re-accrual), 200-query cost bound (Phase G:
0 ticks), worker release + reap accounting.
