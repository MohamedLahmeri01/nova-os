# NOVA OS — Beta V1.3 Persistence Contract (audited, not assumed)

Source-verified storage contract for /disk + /nova.
Provenance: code read (paths below), live guest
tests (this campaign), prior demo screenshots.
Clean-poweroff persistence does NOT imply crash
consistency — stated here, not footnoted.

## 1. Backing and init

- `/disk` → FAT32 on `fat32.vdi` (IDE-0-1, ATA
  slave); `/nova` → NOVA-FS on `nova.vdi`
  (IDE-1-1, ATA secondary slave); `/ext` →
  `ext4.vdi` read-only. Wiring: pm_main.c
  ata_slave_*/ata_nova_* callbacks into
  fat_init/nova_init (~lines 352/364).
- ATA PIO + BMIDE DMA, LBA28 ≤128 GiB, bounded
  spins (ata.c). No IRQ-driven storage.

## 2. Operation semantics (shell-visible truth)

- `put path text`: creates (FAT/NOVA; never EXT),
  writes bytes at offset 0 through to disk per
  512 B kernel-bounce chunk (syscall.c do_write:
  copy_from_user → grow node buffer → driver
  write → refresh cache). NO SHRINK: overwriting
  with shorter text leaves the old tail on disk
  (API-documented, verified live this campaign).
- `cat path`: reads via the materialized node
  copy; exact bytes.
- `ls dir`: graft readdir (8.3 UPPERCASE names
  on FAT — `v8.txt` lists as `V8.TXT`).
- `rm path`: delete (FAT 0xE5 entry; NOVA frees
  blocks+inode); read-after-remove → ENOENT.
- `mkdir/chmod` on grafts: mkdir works
  (FAT/NOVA); chmod refuses EROFS on grafts.
- Limits: path ≤256 B, name ≤32 B, file ≤64 KiB
  (FS_MAX_FILE; over-limit refuses ENOSPC, never
  truncates silently); offsets u32 but reachable
  range capped by the size check (no wrap: end ≤
  cap enforced before use).
- Errors: ENOENT/EACCES/EROFS/EIO/ENOSPC/EFBIG,
  fail-closed, per-op errno (Phase K texts).
- `put` with empty text prints usage (empty files
  not creatable from shell; driver-level create
  of empty files works — syscall layer only).

## 3. Flush / ordering / poweroff assumptions

- Every successful write chunk reaches the
  virtual platter before `put` returns: no write-
  back cache exists (single-entry read cache,
  invalidated on any write; NOVA-FS checkpoints
  synchronously). A file read back after close
  is byte-exact on media.
- Clean poweroff/reboot preserves all completed
  writes (proven: demo + this campaign).
- Mid-write power loss: NO GUARANTEE (FAT has no
  journal; NOVA-FS journal recovery path is not
  power-cut tested). Torn writes may leave old
  tail + partial new bytes; fsck is audit-only.
- Never share a VDI between two VMs; never
  snapshot-merge mid-run for tests.

## 4. What PERSIST-OK proves (and does not)

- Proves, every boot: driver-level create/write/
  grow/reread/delete/gone on BOTH live volumes,
  byte-exact over 1024 B, NOVA fsck-clean after —
  the exact calls the syscall layer makes.
- Does NOT prove: cross-reboot survival (demo
  procedure proves that, screenshots), crash
  consistency, wear behavior, large-file paths,
  or concurrent-writer safety.
