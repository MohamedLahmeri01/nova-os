# ADR-0049: On-disk chmod for NOVA-FS (13d)

Status: accepted (Phase 13d). Date: 2026-10-05.

## Context

Permissions (10c) left graft chmod at EROFS: ramfs modes changed
in memory while NOVA-FS modes were frozen at mkfs values. The
native driver already journals metadata writes (mkdir link
counts), so a journaled mode write is the same idiom, not new
machinery.

## Decision

- `nova_chmod` (type bits preserved, perm bits replaced) stages
  the table block and commits a single-pair journal transaction
  (checksum recomputed by `itable_patch`, like every metadata
  write). Owner-or-root enforced inside, from the inode's uid.
- `fs_chmod` routes `/nova` paths through (FAT/EXT4 stay EROFS:
  no identity/no writes respectively); jailed callers keep
  ENOENT invisibility (routing only fires globally).
- Proofs: guest round-trips mode via `nova_inode_info` on a
  created file (stamp + chmod + re-read) and denies a stranger
  on the 1000-owned NOTE (perm selftest); host T12 mirrors the
  round-trip on its image copy.

## Evidence

Guest `NOVA-OK`/`PERM-OK`; `nova test` 20/20.

## Hard lessons

- New cross-file callees break host links, not guest builds:
  `nova.c` gained `fs_current_*`, and T12 links nova.c
  standalone. First tried root stubs in the test, then hit
  duplicates once `file.c` joined the link for routing --
  resolved by letting the real `file.c` provide them through
  the (already stubbed) NULL scheduler. Prefer widening the
  link over forking behavior with stubs.
- Fixture determinism keeps paying: identity bytes ride the
  existing checksum machinery with zero fsck changes (both
  checkers ignore the fields; old images read root).

## Consequences

- FAT chmod stays EROFS (attribute model is a different
  mapping); ext4 stays read-only throughout.
- Next: 13e candidates (sockets/DNS, IRQ-driven virtio) or release.
