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
FS nodes/fds         | BSP-only (single reader) | no     | no          | VFS locking passes (later phase)
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

## What AP scheduling requires (checklist for that ADR)

1. spinlock (done) + per-CPU data (designed, §21/ADR-0069).
2. Heap + PMM locks (contention-heavy: measure first).
3. Runqueue + sleep-list under one lock; wakeup paths audited.
4. TLB shootdown (IPI) before any page-table sharing changes.
5. Re-audit every "BSP-only" row above; convert or justify.
6. Stress: contention selftests at 2+ CPUs (SYNC-OK pattern).
