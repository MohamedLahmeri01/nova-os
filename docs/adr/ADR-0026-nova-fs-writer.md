# ADR-0026: NOVA-FS writer — journaled mutators, write-through

Status: accepted (Phase 7f-2). Date: 2026-10-03.

## Context

The 7f-1 reader proves layout parsing; the disk is still frozen at
mkfs time. Phase 7f-2 adds the mutating half: allocator, journaled
create/write/delete/mkdir, syscall write-through, shell `put` — with
the power-cut story (recovery replays committed work, torn tails are
absent, never garbage).

## Decision

Journal (`kernel/fs/nova.c`): descriptor (homes + per-block crcs, all
in sector 0 = the atomic unit) + data + dedicated commit block;
synchronous commit+checkpoint per transaction; 8-pair cap matching an
8-buffer staging pool; recovery collects valid descriptors and
replays in SEQ order (scan order is not time order after cursor
wrap). Allocator over the block/inode bitmaps with SB counts updated
in the same transaction. Mutators grow extents (contiguity-
preferred, 4 inline max), split dir slots (never leave zero tails),
zero fresh blocks, recompute inode crcs. Single-block dirs stay the
v0.1 limit (ENOSPC when full; fixture + mkdir'd dirs carry slack).
Gap writes rejected (ENOSPC). Guest selftest is idempotent across
boots (prologue cleanup, EEXIST-tolerant mkdir) and hermetic
(scratch deleted). Host T12 adds multi-block byte checks,
offset-merge, ENOSPC honesty, and torn-commit remount semantics.

## Evidence

Guest `NOVA-OK` incl. 100B byte-exact + 3000B round-trips; second and
third boots of the same VDI pass (recovery correct); typed
`put /nova/msg.txt hello` + `cat` -> `hello`, surviving a power
cycle (durability); `nova test` 12/12.

## Hard lessons

- Recovery MUST replay in seq order: block-order replay resurrects
  deleted files after cursor wrap (second-boot `NOVA-FAIL create`).
  Found by booting twice, not by reading code.
- Dir-slot splitting is load-bearing: a 16B entry in a 32B slot
  without a remainder header scans as corruption (rl==0 -> EIO).
- `rem - 32 < 8` with rem < 32 underflows uint32 and the "exact fit"
  math then overflows a 4KB staging buffer into the staged itable
  (silent inode corruption one buffer over). Guard every subtraction
  that feeds a length (`at + 8 <= BLOCK` style preconditions).
- Fixture dirs need free slack (packed dirs ENOSPC the first child);
  mkdir'd dirs need it too (same rule, same fix).
- Per-call write bound counts BLOCKS touched (<= 4 = 8 pairs max),
  not bytes (a 6000B write is 2 blocks and fits fine).
- Boot selftests on persistent VDIs must be re-runnable (prologue
  cleanup + EEXIST tolerance), or T6-after-T5 and plain reboots fail.

## Consequences

- No truncate/rename, no dir growth (ENOSPC), no gap writes, no
  fsck yet (7f-3). Kernel at 126/127 loader sectors: growth needs
  the loader budget next (see Phase 8).
- Next: 7f-3 fsck (host tool first), then Phase 8 drivers (DMA/IRQ).
