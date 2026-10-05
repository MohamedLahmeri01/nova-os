# ADR-0048: Native file identity — ext4/nova uid/gid/mode (13c)

Status: accepted (Phase 13c). Date: 2026-10-05.

## Context

Ownership (10c) left disk files opener-owned: every `/ext` and
`/nova` open stamped the opener, whatever the inode said. The
identity was on disk all along (ext4 slots; NOVA-FS had a uid
stub awaiting exactly this).

## Decision

- Fixtures carry real identity (`NOTE.TXT` → uid 1000, gid
  100, mode 0600 on both images; everything else stays
  root/0644): `mkext4.py`/`mknova.py` `inode()` take uid/gid.
  NOVA-FS activates gid at [24:2] (ext2-style; bytes 20-25 were
  free, both fscks ignore them, old images read gid 0=root).
  NOVA-FS.md documents uid live + the gid field.
- `ext_inode_info` / `nova_inode_info` read uid/gid + perm
  bits from resolved inodes (standard ext4 slots; NOVA-FS doc
  offsets). `do_open` graft branches override the blank node's
  opener-owned defaults after good data (fail-closed EIO).
  `nova_create_file`/`nova_mkdir` stamp the creator
  (`fs_current_*`, same pattern as ramfs).
- FAT stays opener-owned (no identity on FAT: final, not
  deferred). `fs_chmod` stays EROFS on grafts (write-through
  needs inode RMW + checksums: future). Non-root stamping and
  the do_open override call sites are reviewed, not executed
  (user-path wall, same compromise as 10b/10c).

## Evidence

Guest `EXT-OK`/`NOVA-OK` (fixture asserts + create-stamp);
host T11/T12 mirror the asserts on the same images;
`nova test` 20/20.

## Hard lessons

- New cross-file callees break host links, not guest builds:
  `nova.c` gained `fs_current_*` (defined in `file.c`), and
  T12 links nova.c standalone. Resolved with root-returning
  stubs in the test (documents host==boot-context), not by
  dragging half the kernel into the link.
- Fixture determinism is load-bearing: both generators stay
  byte-identical given the same inputs, so identity bytes ride
  free (checksums recomputed by the tools).

## Consequences

- Disk files now enforce for real (a 0600 NOTE denies
  non-owners at open, not just in ramfs).
- Next: 13d candidates (sockets/DNS, on-disk chmod, IRQ virtio)
  or release.
