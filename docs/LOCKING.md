# NOVA OS — Locking Policy (Phase 0.3-E)

Status: normative for reviews. Single executor today (APs parked);
every entry states what changes when APs schedule.

## Primitives

- `struct spinlock` (`kernel/sync/`): xchg test-and-set + pause.
  Plain `spin_lock/unlock/trylock` + irqsave pair (IF-saving
  restore — blind `sti` inside a handler re-enables
  mid-handler; see ADR-0061). Non-reentrant (self-deadlocks
  by design); `owner` is diagnostic, not enforcement.
- IF discipline (`cli/sti` sections): used where no lock object
  exists yet (kbd ring head/tail, sock waiter check-and-park).
  Equivalent to a lock only while a single executor holds.
- Rule: sleep is forbidden while holding a spinlock or inside
  an IF-held section (spins never block; waiters park lock-free).

## Per-structure rules

```text
structure            | guard today              | IRQ?   | sleep held? | SMP future
---------------------|--------------------------|--------|-------------|---------------------------
sched runqueues      | BSP-only (no guard)      | N/A    | no          | sched_lock + per-CPU queues
sleep list           | BSP-only (tick: IF=0)    | wake   | no          | same lock as runqueues
net ring (last_used) | net_rx_lock (IF-saving)  | pump*  | no          | lock stands; re-audit fences
net pending queue    | net_rx_lock              | pump*  | no          | lock stands; re-audit fences
sock slot + waiter   | IF-held check-and-park   | wake   | no (park after) | lock per-socket or global
kbd ring + waiter    | cli sections / IF-park   | push   | no (park after) | same pattern as sock
heap/PMM             | BSP-only (no guard)      | no     | no          | heap_lock + pmm_lock first
FS nodes/fds/drivers | g_fs_lock (VFS big lock, irqsave, syscall granularity) | no | no | lock stands; re-audit fences
compositor pool      | BSP-only                 | mouse† | no          | gui_lock (later phase)
```

`*` tick pump runs IF=0 to completion today; device-ISR ring work
was built and reverted (ADR-0061) — no ISR drain exists.
`†` mouse press edge composes (focus+present) under IF=0; same
single-executor argument as everything else.

## Ordering

Today: no nesting except RX lock inside drill/ISR-adjacent paths
(which provably cannot contend: holder ⟹ IF=0 ⟹ ISR absent).
Rule for later: leaf-first ordering documented per new lock
(runqueues > sleep > net > sock/kbd > heap > fs); lockdep-style
review on every addition. Double-lock panics where cheap;
trylock + EBUSY where blocking is wrong (waiters).

## Derived nesting (audited, not aspirational)

Actually-nested acquisitions in-tree (v0.4 audit):
1. `net_rx_lock` → `net_rx_pump_locked` (drill, ISR): single
   depth (locked variant takes no lock of its own).
2. Waiter wakeups under IF-held sections (sock/kbd park):
   `sched_wakeup` takes no lock (list ops under the
   caller's IF), so no nesting.
3. `spin_lock_irqsave` → `spin_lock`: same object, not two
   locks (IF save is state, not a lock).
Everything else is depth-1. Any new nesting must name its
depth here before merge.

## Sleep and scheduler locks

- Sleep is forbidden while holding any spinlock or inside
  any IF-held section. Rationale: holders run IF=0, so a
  sleeper would never be woken by the tick it needs (the
  tick pends) — deadlock by construction, not by race.
  Park-then-sleep order is mandatory (waiters park
  lock-free; verified in sock/kbd loops).
- There is no scheduler lock (runqueues + sleep list are
  BSP-only). The first one arrives with AP scheduling,
  not before.
- No lock may be held across `sched_yield`,
  `sched_switch_to`, `sched_sleep*`, or `thread_exit`.

## Interrupt-context rules

- IRQ handlers may take `net_rx_lock` (IF-saving: saves 0,
  restores nothing) and run pump/wakeup paths. They may
  NOT take waiter-owned locks (none exist), sleep, or
  allocate (heap/PMM have no IRQ-safe paths).
- `arch_irq_save` callers must pair with restore on every
  path (early returns included); the saved word is
  caller-owned stack state, never global (globals would
  corrupt under nesting).

## Reference-count rules

- Counts only move under the owner's lock-or-IF discipline:
  fs nodes (lookup/create/unlink paths, BSP-only today),
  thread graveyard (exit/handoff + reap sites),
  socket generations (open/close/deliver).
- Increment before publishing a new reference; decrement
  after the last use; free at zero with no resurrection
  path (freed objects never re-enter tables — the
  addrspace-PD zeroing rule is the model).
- No checked refcount may wrap (u32 counters with
  bounded populations: 8 sockets, 16 threads, 16 fds).

## What AP scheduling requires (checklist for that ADR)

1. spinlock (done) + per-CPU data (designed, §21/ADR-0069).
2. Heap + PMM locks (contention-heavy: measure first).
3. Runqueue + sleep-list under one lock; wakeup paths audited.
4. TLB shootdown (IPI) before any page-table sharing changes.
5. Re-audit every "BSP-only" row above; convert or justify.
6. Stress: contention selftests at 2+ CPUs (SYNC-OK pattern).
