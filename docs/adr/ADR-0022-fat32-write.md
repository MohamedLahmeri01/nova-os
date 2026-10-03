# ADR-0022: FAT32 write — allocator, write-through, shell `put`

Status: accepted (Phase 7d). Date: 2026-10-03.

## Context

The FAT driver reads but the disk is frozen at mkfat time: no file
creation, no writes, mkdir refused with EROFS. Phase 7d makes /disk
writable (still no delete-user-visible... delete exists for test
hygiene) with crash-safe layering: node copies stay caches, the disk
is truth.

## Decision

`ata_write_sector` (WRITE SECTORS 0x30, same polling + BSY confirm);
selftest writes slave LBA 100 (past the 64-sector volume, inside the
1MB image) with pattern/verify/restore. FAT mutators: `alloc_chain`
(zeroed clusters, atomic ENOSPC with rollback), `free_chain`,
`fat_create_file`/`fat_write_at` (grow-only, cap FS_MAX_FILE,
partial-sector read-merge), `fat_delete` (chain free + 0xE5),
`fat_mkdir` (. / .. + entry), size/cluster entry patching. One
single-entry sector cache, invalidated on every write. Syscalls: disk
open accepts WRONLY/RDWR/CREAT; writes go write-through per 512B
chunk (node buffer grown first so OOM fails before disk mutation);
mkdir routes to `fat_mkdir`. Shell gains `put <path> <text>`.
Guest selftest is hermetic (scratch files created, verified,
deleted). Host T10 covers the same plus full multi-cluster byte
checks, offset-overwrite merge, and post-mutation fixture integrity.

## Evidence

Guest `FAT-OK` (includes 100B byte-exact + 3000B 6-cluster round
trips); typed `put /disk/msg.txt hello` + `cat /disk/msg.txt` ->
`hello`; `nova test` 10/10.

## Hard lessons

- Materialize-on-open + NULL buffer for empty files: skip the fetch
  when size is 0 (fat_read_file rejects NULL dst).
- Fresh dynamic VDIs stall the first write (host allocation): one
  transient ATA-FAIL on a fresh image; spins raised 500k -> 2M
  (still loud on dead drives, absorbs the stall).
- split_parent mapped every dir error to ENOENT: propagate ENOTDIR.
- Alloc rollback must not touch the free count (decrement happens
  only on success).
- `bytearray` slices copy (mkfat, again); BPB total-sectors at 32.

## Consequences

- No truncate, no rename, no fsck, no wear concerns (test fixture).
  Directories grow but never shrink (0xE5 slots reuse).
- Next: EXT4-read, NOVA-FS design; then Phase 8 drivers (DMA/IRQ).
