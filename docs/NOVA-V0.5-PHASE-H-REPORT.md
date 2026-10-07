# NOVA v0.5 Phase H Report — Filesystem Concurrency (Outcome B)

## 1. PHASE H STATUS

PASS (Outcome B: concurrent FS execution supported under
a VFS big lock; ADR-0076 runtime contract superseded).

## 2. BASELINE

`fee0a2b` clean; reproduced 23/23, CORPUS-OK 7/7, gate
PASS, `proc=140 sum=140`, `hog=30 low=10 early=0`,
wake/resume proof before any modification.

## 3. EXISTING CONCURRENCY CONTRACT

ADR-0076: single-threaded-only, no locks, no runtime
enforcement, stress gated on locking. Retired as the
runtime rule by this phase (kept as pre-locking record).

## 4. FILESYSTEM INVENTORY

VFS (`file.c`: nodes/tree links, descriptions+offsets,
fd tables, refs), 4 drivers with static mutable state
(FAT cache/geometry/free-counts; EXT4 geometry+4K
scratch; NOVA-FS journal/bitmaps/counters/scratch;
ramfs tree/buffers/sizes), ATA PIO/DMA polled I/O,
fd tables per process. Full table in
`docs/FILESYSTEM-CONCURRENCY.md` §2.

## 5. OBJECT OWNERSHIP

Nodes: tree/VFS-lock; descriptions: fd slot; fd tables:
process; materialized graft nodes: description; driver
caches/geometry/journals: VFS lock. Direct `fs_*`
callers: boot-phase serialization (audited, documented).

## 6. OBJECT LIFETIME

Linked vs detached-unlinked nodes; refs = open
descriptions + jail holds; free at refs==0 when
unlinked. Threads hold no node pointers across
syscalls (per-call lookup). No resurrection paths.

## 7. REFERENCE COUNT AUDIT

Plain ++/-- sound: every mutation under the big lock
or single-threaded by phase. Increment-before-publish
(fs_file_alloc refs at creation), decrement-after-use,
free-at-zero. Populations bounded (16 fds). Proven by
refs==0 asserts post-stress.

## 8. VFS CONCURRENCY AUDIT

All VFS entry points identified (12 handlers); each
wrapped. Inner `fs_*` take no locks (nesting audited
absent: `resolve_parent` nests lookup, which is why
per-function locking was rejected).

## 9. FILE/OFFSET AUDIT

Offset in description, mutated beside I/O under lock.
Shared fd = shared offset (proven byte-exact sums);
separate opens = separate offsets. No POSIX claims
beyond serialized per-call atomicity.

## 10. DIRECTORY AUDIT

Child-list link/detach under lock; readdir stable per
call; concurrent create/unlink during iteration yields
only valid rc (0/EEXIST/ENOENT) — proven, no UAF.

## 11. FAT32 AUDIT

Cache+geometry+free-counts under lock; RMW atomic per
call; concurrent same-file reads byte-identical;
40-open (later 120+) cycles clean.

## 12. EXT4 AUDIT

Read-only; geometry+scratch under lock; concurrent
reads byte-identical. No write support added.

## 13. NOVA-FS AUDIT

Journal/bitmap/counters under lock; commits atomic per
call; concurrent reads byte-identical; post-stress
fsck clean (ticks=2..3).

## 14. RAMFS AUDIT

Primary stress target: tree, buffers, sizes, refcounts
under lock; write atomicity per call (128 pure 64B
blocks proven); grow-by-doubling atomic per call.

## 15. LOCKING DESIGN

One global VFS spinlock, irqsave, syscall granularity.
Chosen over per-inode (nesting/lifetime cost), plain
spin (same-prio waiter starves preempted holder —
deadlock by design), per-`fs_*` locking (resolve_parent
nesting deadlocks non-reentrant lock).

## 16. LOCK HIERARCHY

Single lock: no order needed, depth-1 everywhere,
LOCKING.md leaf position holds vacuously.

## 17. BLOCKING/I/O ANALYSIS

Zero sched_* calls in `kernel/fs/`; disk polls bounded
(ATA_SPIN_MAX); walks step/depth/rec_len-capped. irqsave
required (not caution): IF=0 holder always completes,
so waiters (same prio, RR) always progress.

## 18. INTERRUPT-CONTEXT ANALYSIS

No FS path runs in IRQ (grep-verified); disk completion
polled; ATA fixed to save/restore (was blind sti —
the deadlock below). `net_rx_lock` pair audited
balanced (net-only, untouched).

## 19. UNLINK/RMDIR ANALYSIS

Open-unlinked files keep working (refs gate freeing);
rmdir refuses non-empty/held/root. Proven + cleaned
(no dangling names; `/st` gone, ENOENT verified).

## 20. MOUNT/UNMOUNT ANALYSIS

Static grafts, immutable routing, no unmount exists.
Nothing to race.

## 21. IMPLEMENTATION

`g_fs_lock` static in syscall.c (guest-only linkage),
init in `syscall_init`, 12 rename+wrapper pairs
(table/ABI untouched), stress `fs_concurrency_selftest`
(frames, shared scratch mapping, same-prio pair),
`FSSTRESS-OK` gate in both marker lists + pm_main hook
post-sync.

## 22. STRESS TESTS

open/close ×200, shared-fd exact sums (8192/8192),
atomic blocks (128 pure), metadata storm (valid rc),
3-graft byte-identical ×20, pointer battery (EFAULT),
refs/mode asserts, cleanup, fsck. ~110-132 ticks.
Deterministic (no timing asserts; watchdogs only).

## 23. CORRUPTION/FSCK VALIDATION

Post-stress: pattern file intact, sizes exact, refs 0,
tree walkable, `nova_fsck()` clean, graft spot reads
equal. Host T13 + corpus untouched and green.

## 24. DEADLOCK ANALYSIS

Graph: one lock, no nesting, no IRQ acquisition, no
sleep-held → acyclic by construction. Empirically: the
battery deadlocked pre-fix (see §28 bug 1) and passes
post-fix across 5+ boots.

## 25. SECURITY

Permission checks run inside the same locked bodies in
fail-closed order — checks and uses share each
critical section (metadata TOCTOU closed for
non-sleeping paths). Lock adds no privilege logic.
W8 CLOSED (residuals documented, none vulnerabilities).

## 26. SMP CONSIDERATIONS

Lock is bus-correct (xchg) and becomes cross-CPU as-is;
per-CPU tick isolation + re-audit of "serialized"
claims required at SMP. Nothing added now.

## 27. PERFORMANCE

Stress ~110 ticks (~1.1 s) for ~2000 locked ops;
fsck 2-3 ticks; all PERF deltas zero; boot +~2 s.
Coarse by decision (correctness first); contention
measured acceptable, no granularity work.

## 28. BUGS FOUND/FIXED

1. PRODUCT BUG (pre-existing, found by stress):
   ATA blind `sti` reopened preemption inside the FS
   critical section → same-prio waiter spins IF=0 while
   holder starves = permanent deadlock (proven via
   VBox `getregisters`: EIP in `spin_lock`, IF=0).
   Fix: save/restore at 4 ATA sites. Without the
   battery this would have shipped.
2. TEST BUG (mine): whole-phase CR3 borrow is not
   switch-safe (switches reload kernel CR3) → -EFAULT
   after first slice expiry. Fix: per-call borrow under
   IF held. Rule documented (no concurrent whole-phase
   borrows, ever).
3. TEST BUGS (mine, pre-boot): stale-scratch checks,
   ebx/ecx swap, clobbered decls — all caught on
   review/lint before boot. No baseline behavior
   changed at any point.

## 29. DOCUMENTATION

`docs/FILESYSTEM-CONCURRENCY.md` (new, 22 items),
`docs/adr/ADR-0079-fs-concurrency.md` (new, supersedes
ADR-0076 runtime contract), LOCKING.md FS row,
SECURITY-MODEL W8 closed, CHANGELOG Phase H entry.

## 30. ADR

ADR-0079 (new): old contract, design, alternatives,
deadlock strategy, I/O strategy, guarantees,
residuals. ADR-0076 kept as record (not deleted).

## 31. FILES CHANGED

`kernel/syscall/syscall.c` (lock+12 wrappers+stress),
`kernel/drivers/ata.c` (4× save/restore),
`kernel/fs/fs.h` (stress decl), `kernel/pm_main.c`
(hook), `tools/nova.py` (FSSTRESS-OK gates),
`docs/FILESYSTEM-CONCURRENCY.md`,
`docs/adr/ADR-0079-fs-concurrency.md`, `docs/LOCKING.md`,
`docs/SECURITY-MODEL.md`, `CHANGELOG.md`, this report.

## 32. FINAL TEST MATRIX

23/23 PASS, CORPUS-OK 7/7, GATES-OK, TRACEFETCH-OK,
TRACEFETCH-ADV-OK, RUNTIME-OK, FSSTRESS-OK, PERF-OK,
lint clean (1 pre-existing warning), diff-check clean.

## 33. FINAL GATE

`ci/gate.py` PASS post-change (env+lint+build+image).

## 34. COMMIT

Single Phase H commit (hash below), tree clean after.

## 35. PUSH

NOT PUSHED

## 36. REMAINING LIMITATIONS

FILESYSTEM-CONCURRENCY.md §22 (coarse lock,
direct-`fs_*` review rule, no priority inheritance,
per-call borrow rule, corrupt-disk posture).

## 37. NEXT AUTHORIZED STEP

NEXT AUTHORIZED STEP: Phase I requires separate approval.
