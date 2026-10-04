# ADR-0027: fsck — host verifier plus in-guest audit

Status: accepted (Phase 7f-3). Date: 2026-10-03.

## Context

A journaled FS without an independent checker is unverifiable: the
driver grades its own homework (it passed NOVA-OK while the fixture
had zero free root slots — the checker mindset caught what asserts
missed). Phase 7f-3 adds verification on both sides: a host tool for
CI/mutations and an in-guest audit behind `fsck`.

## Decision

`tools/novafsck.py` (stdlib only): SB copies, journal coherence
(torn tails reported, not errors), bitmaps-vs-reality both
directions, inode checksums, dir chains, orphans, free-count
agreement. Kernel `nova_fsck()` (same checks, static 128/1024-bit
ref sets, explicit stack) behind `SYS_FSCK` + shell `fsck`; the
guest selftest asserts it returns 0 (self-audit gate). T13 runs the
host tool on the pristine fixture (must be clean) plus 4 mutation
classes (must each be detected): inode checksum, dir rec_len,
cleared data bit, killed SB copy.

## Evidence

Host: pristine `fsck: 0 issue(s)`; all 5 probe mutations detected
(incl. orphans + single-copy degradation); T13 green. Guest: typed
`fsck` -> `fsck ok`; `nova test` 13/13.

## Hard lessons

- GCC vectorizes innocent zeroing loops into SSE (`xorps`); CR4.OSFXSR
  is never set and no XMM state is context-switched, so the first
  `fsck` run died with #UD (found via disassembly of the fault eip).
  Fix is global: `-mno-sse -mno-mmx` in CFLAGS32 (kernel AND
  userspace), verified zero SIMD in both binaries. SIMD stays out
  until the context switch saves it.
- Checker found real slack-slot and orphan-class issues the driver's
  own asserts missed: independent implementation > self-check.
- Kernel at 126/127 loader sectors: the next feature needs loader
  budget work (multi-load or diet) — flagged for Phase 8.

## Consequences

- Verify-only (no repairs): 7f-3 closes the FS track as designed.
  Repair (link/bitmap surgery) is future work, not planned.
- Storage complete: RAMFS, FAT32 rw, EXT4 ro, NOVA-FS journaled rw,
  fsck both sides. Next: Phase 8 drivers (DMA/IRQ disk I/O replaces
  polling; loader budget).
