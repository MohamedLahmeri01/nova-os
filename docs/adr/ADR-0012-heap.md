# ADR-0012: Kernel heap — slabs + large runs, tagged frames

Status: accepted (Phase 2c). Date: 2026-09-29.

## Context

Scheduler/process/VFS need kernel dynamic memory. Constraints: no
libc, 32-bit, 1GB box, host-testable sources, exact accounting.

## Decision

Slab caches (8..2048, 8-aligned) with in-frame metadata + freelist;
larger sizes via contiguous PMM runs with magic headers. Every owned
frame carries a carved 1B tag (other/slab/large/meta); kfree
dispatches on the tag and verifies magic/range/alignment, panicking
on corrupt, foreign, or double-freed pointers (double-free caught by
an always-on freelist walk). Frame addresses cross an HPTR offset
(identity on guest) so the same sources run on the 64-bit host test
and stay ready for a higher-half kernel. PMM gained a contiguous API
(bitmap: real first-fit; buddy: explicitly unimplemented stub — it is
the parked alternative). `kheap_trim` releases all empty slabs, giving
exact PMM restoration (verified equal frame counts, guest and host).

## Evidence

- Guest: `HEAP slabs=0 live=0 large=0` + `HEAP-OK` (drain, OOM,
  interleave, canaries, trim, exact restore), T5/T6 green.
- Host (`nova test` T7): 64MB arena, 50k-op churn at ~6M ops/s with a
  reference live-set, exact restore, `VERDICT: heap correct`.
- Bugs found by testing (all fixed, all with regression coverage):
  backward-built slab slots violating the alignment check; trim/all
  callers disagreeing on spare retention (now: kfree keeps one warm
  spare, trim releases all); host segfault from truncating a 64-bit
  pointer before subtracting the translation offset; test-harness
  arena 1MB off the real box (Phase 2a pattern repeated, same fix).

## Consequences

- `pmm_init` takes an optional metadata buffer (NULL = carve; host
  passes malloc'd) — guest-physical assumptions no longer leak into
  shared code paths.
- No locking (single-core, no IDT yet): SMP must add it with the
  scheduler. Known limitation, not an oversight.
- 2048B class wastes ~2KB/frame; slab sizing tunes when measurements
  demand it.
