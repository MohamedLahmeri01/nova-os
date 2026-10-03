# NOVA-FS — Native Filesystem Design (Phase 7f)

Status: **design, not implemented**. Version 0.1. Date: 2026-10-03.
Parent decision: ADR-0006 (VFS first, NOVA-FS last). This note turns
"journaled, checksummed, crash-consistent" into a buildable layout,
informed by implementing RAMFS + FAT32 (read/write) + EXT4-read.

## 1. What the three implementations taught us

| Lesson | Source | Design consequence |
|---|---|---|
| Chain walks cost seeks; per-entry sector re-reads cost VM exits | FAT32, 7c | Extent-based data mapping, 1-block sector cache minimum |
| Case-folding belongs to no layer by accident | FAT vs EXT4 | Names are opaque bytes; normalization is a mount option, never the FS |
| Parsed-not-assumed geometry (magic, sizes, counts) | all three | Every number revalidated at mount; mismatch = refuse, not panic |
| Whole-file materialize keeps VFS simple | grafts | Keep the vnode model; NOVA-FS plugs the same open/read/write path |
| Deterministic fixtures make bugs reproducible | mkfat/mkext4 | `mknova.py` from day one; corruption-injection corpus is a merge blocker (ADR-0006) |
| Slow I/O without an IDT triple-faults | 7c | Driver init order is part of the FS design, not trivia |
| Free-count honesty needs a single source of truth | 7d FSInfo | Allocator owns the free count; informational copies are write-back only |

Rejected from the start: journal checksumming before we measure cost,
COW/snapshots (ADR-0006), compression, encryption at rest (goes in a
stacked layer, not the base format), long-filename hacks (names are
bytes, 255 max, NUL and `/` forbidden).

## 2. Layout (4KB blocks, little-endian, single group first)

```text
LBA 0..7     superblock copy 0 (block 0, magic at byte 1024 like ext)
LBA 8..15    journal area start (size in superblock, default 64 blocks)
...          block bitmap, inode bitmap, inode table (128B inodes)
...          root directory block(s), data extents
last 8 LBAs  superblock copy 1 (redundant; mount uses copy 0 unless
             its checksum fails, then copy 1 — never trust one copy)
```

Superblock (1 block, selected fields):

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `NOVAFS01` |
| 8 | 4 | block size (4096, enforced) |
| 12 | 4 | total blocks |
| 16 | 4 | free blocks (informational; bitmap is truth) |
| 20 | 4 | total inodes |
| 24 | 4 | free inodes (informational) |
| 28 | 4 | root inode (= 2, ext-compatible numbering) |
| 32 | 4 | journal start block |
| 36 | 4 | journal blocks (default 64) |
| 40 | 4 | journal sequence (monotonic; replay beats it) |
| 44 | 4 | features ro-compat mask (unknown bits = mount read-only... refuse: see §5) |
| 48 | 4 | checksum (crc32c of block 0..47 + magic) |
| 64 | 16 | uuid (fixed at mkfs, deterministic fixture uses all-`NOVA` pattern) |
| 80 | 16 | volume label (bytes, space-padded) |

Inode (128B, ext-compatible numbering from 1, first usable 11):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | mode (file/dir bits + permission stub) |
| 2 | 2 | uid stub (0 until identity exists) |
| 4 | 4 | size low |
| 8/12/16 | 4+4+4 | atime/ctime/mtime (fixed at mkfs in fixtures) |
| 26 | 2 | link count |
| 28 | 4 | block count (512B units, fsck cross-check) |
| 40 | 60 | extent tree root (4 inline extents; see §3) |
| 100 | 4 | generation (delete/recreate disambiguation) |
| 108 | 4 | size high |
| 112 | 4 | checksum (crc32c of bytes 0..111) |

Directory entry (like ext, simplified): `ino u32, rec_len u16,
name_len u8, type u8, name bytes`. No hashing in v0.1 (linear scan;
measured before indexing — FAT taught us linear is fine under ~100
entries, and our fixtures stay there).

## 3. Extents (why not FAT chains, why not indirect blocks)

FAT chain-following turns a 6-cluster file into 6+ dependent reads;
ext-style indirect blocks add a level of pointer chasing the reader
must revalidate. NOVA-FS maps data as extents:

```text
extent: { start_block u32, length u16 in blocks, flags u16 }
```

- 4 extents inline in the inode (covers ≤ 4 runs; fixtures: 1 run).
- Overflow: one extent-index block (sorted array of 340 12B index
  entries — logical, leaf, reserved — pointing at leaf extent
  blocks; depth ≤ 2 enforced).
- Unwritten extents (preallocation without zeroing reads): flag bit,
  reads return zeros, first write converts. Justification: zeroing
  on alloc cost us measurable boot time in 7d selftests.
- Max file: min(FS_MAX_FILE growth path, 2^32 bytes). v0.1 enforces
  the existing 64KB VFS cap; raising it is a VFS change, not a
  format change.

## 4. Crash consistency (ordered metadata journal, NOT full data)

Full data journaling doubles write traffic; writeback journaling
exposes stale data after crashes. v0.1 journals **metadata only,
data ordered before it** (ext3-ordered semantics):

1. Data blocks of a transaction go to their home locations first.
2. Metadata (bitmaps, inodes, dir entries, extent tree) goes to the
   journal as a transaction: descriptor + blocks + commit record,
   each with crc32c.
3. Checkpoint: journal blocks copy home; sequence advances.
4. Mount recovery: replay committed transactions with sequence >
   superblock sequence; discard incomplete (no commit record) ones.

Torn-write bound: one 512B sector is the atomic unit (same as our
ATA driver). A commit record smaller than a sector is atomic iff it
shares the sector with nothing else: commit records get a dedicated
sector. Checksums catch the rest (a torn journal block fails crc and
is treated as absent).

fsck (offline, host-side first): bitmap-vs- Reality cross-check,
link-count recount, extent overlap scan, orphan truncate. The host
fsck shares the layout tables with the kernel reader (same pattern
as validate.c shared guest/host in Phase 5).

## 5. Compatibility and refusal rules

- Unknown incompat feature bits: **refuse to mount** (never guess).
- Unknown ro-compat bits: mount read-only.
- Magic/checksum mismatch on copy 0: try copy 1, then refuse.
- Journal sequence newer than checkpoint with no commit: discard.
- Anything else malformed: errno (`-EIO`), never panic (ADR-0006:
  malformed-image tests are merge blockers).

## 6. Staged implementation (each stage is a mergeable milestone)

- **7f-1 fixture + reader** (`mknova.py`, `kernel/fs/nova.c`):
  read-only, contract asserts + host fuzz vs mutated images, `EXT-OK`
  style `NOVA-OK` gate, graft at `/nova` (read-only like `/ext`).
- **7f-2 writer**: allocator (bitmap + free-count truth), create/
  write/mkdir/delete over the journal; hermetic guest round-trips;
  host byte-exact + power-cut simulation (truncate the image at
  every sector offset, assert mount-or-refuse, never garbage).
- **7f-3 fsck**: host tool first, then guest verify-only pass.
- **Later (not this phase)**: multi-group, extent index depth 2,
  dir indexing, snapshots/COW per ADR-0006.

Estimated fixture sizes (v0.1, 4KB blocks): superblock 1 + journal
64 + bitmaps 2 + inode table 1 + root 1 + hello 1 + docs 1 + note 1
= 72 blocks used of 128 (512KB image, padded to 1MB for VDI like
the other fixtures).

## 7. Open questions (decided before 7f-1 merges)

1. crc32c software table in kernel (+1KB rodata) vs simpler FNV-1a:
   measure; correctness first (any 32-bit checksum), speed later.
2. Time source for mtime: no RTC driver yet — 0 until Phase 8.
3. UID/GID model does not exist: mode bits stored, unenforced.
