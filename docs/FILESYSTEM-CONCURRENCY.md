# NOVA Filesystem Concurrency (v0.5 Phase H — Outcome B)

ADR-0076's single-threaded contract is SUPERSEDED for
syscall-reachable paths (see ADR-0079). Direct `fs_*`
users remain single-threaded by boot-phase order.

## 1. Concurrency contract

Every FS-touching syscall holds the VFS big lock across
its whole body (12 wrappers in `kernel/syscall/syscall.c`:
open/read/write/close/readdir/mkdir/exec/fsck/chmod/
unlink/rmdir/chroot). Concurrent FS syscalls serialize;
each observes atomic handler semantics. Direct `fs_*`
callers (seeds, boot selftests) run with no other FS
user runnable — audited per call site, not locked.

## 2. Object ownership

| Object | Owner | Shared? |
|---|---|---|
| ramfs nodes/tree | VFS lock | yes (threads) |
| descriptions (`fs_file`) | fd slot owner | no (per-fd; shared fd = shared offset by design) |
| fd tables | owning process | yes (threads of one proc) |
| graft materialized nodes | owning description | no |
| FAT cache/geometry | VFS lock | yes |
| NOVA-FS journal/bitmaps/counters | VFS lock | yes |
| EXT4 geometry/scratch | VFS lock (read-only use) | yes |

## 3. Object lifetime

Nodes: linked (tree) or detached-unlinked; refs count
open descriptions + jail holds; free at refs==0 when
unlinked (ramfs), or by unlink/rmdir when linked.
Descriptions die with close. Threads never hold node
pointers across syscalls (looked up per call).

## 4. Reference counting

Plain `++`/`--` (non-atomic) — sound because every
mutation site holds the big lock or is single-threaded
by phase. Rule (LOCKING.md): counts move only under the
FS lock. No wrap (populations bounded: 16 fds, tree
small, opens serialized).

## 5. Locking model

One global VFS spinlock (`g_fs_lock`, static in
syscall.c), irqsave discipline, owner = current thread
(diagnostic). Coarse by decision: correctness first;
contention measured (fsck ticks=2..3, stress ~110-132
ticks for ~2000 ops) — no granularity work justified.

## 6. Lock hierarchy

Single lock: no ordering needed, no nesting possible
(verified: no handler→handler FS calls; inner `fs_*`
take no locks). Depth-1 everywhere. LOCKING.md leaf
position (fs last) holds vacuously.

## 7. Acquisition rules

Acquire once per FS syscall, release exactly once
(single-exit wrappers). Never acquire in IRQ context
(no FS path runs there — verified by grep). Never
re-enter (non-reentrant by design; nesting audited
absent). `was_on` word is per-wrapper stack state.

## 8. Blocking/I/O rules

No FS path sleeps (verified: zero sched_* calls in
`kernel/fs/`; disk polls bounded by ATA_SPIN_MAX;
no unbounded walks — FAT chains step-capped, ext
rec_len-guarded, lookups depth-capped). irqsave (not
plain spin): with IF=0 the holder is non-preemptible
and always completes, so a same-priority waiter can
never starve it — this is load-bearing, not caution.

## 9. Interrupt-context rules

FS lock never taken in IRQ (nothing to take it).
Disk completion is polled, never IRQ-driven, so no
ISR needs the lock. ATA paths use IF save/restore
(Phase H fix — see §23), never blind sti.

## 10. VFS semantics

Shared fd = shared offset, serialized per call (proven:
two-thread 8192B sum exact). Separate opens = separate
offsets. Partial reads/writes keep existing short-count
semantics under concurrency.

## 11. FAT32 semantics

Sector cache + geometry + free counts under the big
lock. Read-modify-write sequences atomic per call.
Concurrent same-file reads byte-identical (proven).

## 12. EXT4 semantics

Read-only; geometry + 4K scratch under the big lock.
Concurrent reads byte-identical (proven). No write
support added.

## 13. NOVA-FS semantics

Journal/bitmap/counters under the big lock; commits
atomic per call. Concurrent reads byte-identical
(proven). fsck agreement post-stress (ticks=2..3).

## 14. RAMFS semantics

Tree links, node fields, buffers, sizes, refcounts all
under the big lock. Write atomicity per call (proven:
128 pure 64B blocks). Grow-by-doubling atomic per call.

## 15. File-offset semantics

Offset lives in the description, mutated only inside
locked read/write beside the I/O: no torn updates, no
lost bytes (proven by exact byte sums).

## 16. Directory semantics

Child-list mutation (link/detach) under the lock;
readdir walks a stable list per call. Concurrent
create/unlink during iteration: only valid rc set
(0/EEXIST/ENOENT), never corruption (proven).

## 17. unlink/rmdir semantics

POSIX-style open-unlinked (descriptions keep working;
last close frees). rmdir refuses non-empty/held/root.
No freed-state dereference (refs gate freeing).

## 18. Mount semantics

Single static mounts (ramfs/FAT/EXT/NOVA grafts, fixed
drives). No unmount exists (nothing to race). Graft
routing is immutable (prefix match).

## 19. Error/unwind rules

Single-exit wrappers: every path releases exactly once
(no early returns inside locked bodies bypass — the
bodies return codes, the wrapper releases). OOM/IO/
permission paths all covered by existing tests.

## 20. Security interaction

Locking changes no permission logic (all checks run
inside the same locked bodies, same order: perm-first,
fail-closed). No race can bypass uid/gid/mode/caps/
chroot: checks and uses share one critical section
(TOCTOU on metadata closed by construction for
non-sleeping paths).

## 21. SMP implications

Single executor assumed: one global lock, no fences
beyond xchg, no per-CPU state. SMP needs the lock to
become cross-CPU (it already is bus-correct), per-CPU
tick isolation, and re-audit of every "serialized"
claim above. irqsave discipline already SMP-shaped.

## 22. Limitations

- Coarse: one FS op at a time (measured OK, not
  scalable by design).
- Direct `fs_*` callers must stay single-threaded
  (rule, enforced by review + boot order).
- Borrowed user mappings must be per-call (switches
  reload kernel CR3 — Phase H test-design lesson;
  whole-phase borrow is forbidden for concurrent
  workers).
- No priority inheritance: FS-sharing threads should
  share priority (documented; shell is single).
- Corrupt-disk infinite loops would hold IF=0
  (pre-existing robustness posture, unchanged).
