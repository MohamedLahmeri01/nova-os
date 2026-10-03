# ADR-0025: NOVA-FS reader (7f-1) — fixture, driver, graft

Status: accepted (Phase 7f-1). Date: 2026-10-03.

## Context

The 7f design (ADR-0024) stages implementation as fixture+reader
first. This milestone builds exactly that: `mknova.py` fixture,
read-only driver, `/nova` graft — mirroring the proven 7c/7e shape
(deterministic image, sector-callback driver shared guest/host,
contract asserts, host fuzz, guest OK-gate, interactive proof).

## Decision

Fixture per the design + 3 layout pointers (itable/bbmap/ibmap at SB
96/100/104 — the design table lacked them; fixed before merge).
Driver (`kernel/fs/nova.c`): parsed SB with copy-1 fallback (raw
total pointer, full re-verification), per-inode crc32c, inline
extents with depth/count enforcement, sparse-hole zeros, linear dir
scans with progress checks, exact names. Fourth disk at IDE P1D1
(secondary slave; IRQ15 already masked). Graft mirrors `/ext`
(RDONLY materialize, readdir, EROFS writes). Host T12 adds
corruption-injection (mutated copies fail clean; both-SB-kill
refuses) per ADR-0006's merge-blocker rule.

## Evidence

Guest `NOVA-OK`; typed `ls /nova` -> HELLO.TXT/DOCS,
`cat /nova/HELLO.TXT` (Shift scancodes) -> `hello from nova`;
`nova test` 12/12.

## Hard lessons

- Layout pointers must be IN the superblock from the start (fixture
  built before the driver noticed; regenerated, caught by inotab==0
  refusal — the validation worked as designed).
- Test-locals reuse masks the real rc (again): fresh sentinels in
  probes, same as 7e.
- Edit discipline with structural refactors: two near-miss body
  deletions caught by immediate re-read (repair before proceeding;
  rule: read the region after every structural edit).
- crc32c agreed first try (known vector e3069283 both sides) —
  pin the vector in a scratch check, not memory.

## Consequences

- Read-only (7f-2 writer next: allocator, journal writes, hermetic
  round-trips, power-cut tests). Fixture journal area stays zeroed.
- `/nova` placeholder + graft shape already supports the writer
  (write-through needs the stored path, like FAT).
