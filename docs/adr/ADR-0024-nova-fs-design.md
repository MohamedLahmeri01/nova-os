# ADR-0024: NOVA-FS design adopted (implementation staged)

Status: accepted (Phase 7f). Date: 2026-10-03.

## Context

ADR-0006 defers NOVA-FS until requirements are proven. Three
filesystems later (RAMFS, FAT32 read/write, EXT4 read), the
requirements are: extent mapping (chain walks cost), parsed-not-
assumed geometry, single-source free counts, deterministic fixtures
with corruption tests as merge blockers. `docs/NOVA-FS.md` v0.1 turns
these into a buildable layout.

## Decision

Adopt the v0.1 design: 4KB blocks, single group first, 128B inodes,
inline extents (4) with depth-2 index overflow, metadata-only
ordered journal with crc32c + dedicated commit sectors, redundant
superblock, linear dirs, names as opaque bytes (255 max). Refusal
over guessing (unknown incompat bits, bad checksums, torn commits).
Staged: fixture+reader (`/nova` graft), writer + power-cut tests,
fsck; multi-group/extents-depth/indexing/snapshots explicitly later
per ADR-0006. No implementation in this milestone (design only).

## Evidence

Design review against the three implementations (lesson table in the
doc); arithmetic verified by execution (72-block fixture estimate,
340-entry fanout at 12B, 44/60B inline budget).

## Consequences

- 7f-1 (fixture + read-only driver) is the next storage milestone;
  FAT32/EXT4 grafts stay untouched.
- Open questions (checksum choice, mtime source, identity model)
  must close before 7f-1 merges.
