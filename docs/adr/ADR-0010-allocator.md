# ADR-0010: Physical allocator — bitmap wins Phase 2a on measurements

Status: accepted (Phase 2a). Date: 2026-09-29.

## Context

ADR-0003 deferred the allocator family to benchmarks. Candidates built
behind one backend interface (`kernel/mm/pmm.h`): next-fit bitmap with
word skipping (`pmm_bitmap.c`) vs binary buddy orders 0..18 with
intrusive lists (`pmm_buddy.c`). Both pure C, both proven correct by a
reference live-set model (double-alloc, OOM stickiness, count
conservation, full restore). Arena mirrors the real box: 261872 frames
(0x100000 len 0x3FEF0000, the actual VBox 1GB map — an early harness
used 262128 and was corrected after cross-checking guest output).

## Measurements (`nova bench-pmm`, Ryzen 5 5500, deterministic seed)

| metric | bitmap | buddy |
|---|---|---|
| managed frames | 261864 | 261544 |
| W1 alloc drain | 173.6M ops/s | 154.5M ops/s |
| W1 free | 440.1M ops/s | 192.8M ops/s |
| W2 churn 200k ops | 48.5M ops/s, largest run 161701 | 46.6M ops/s, largest block 131072 |
| W3 frag absorption | full (248531) | full (248467) |
| metadata carved | 32734 B (8 frames) | ~1.34MB (328 frames) |

Guest (`NOVA-OS-CONSTRAINED`): `PMM-bitmap total=261864 free=261864`,
self-test (drain + OOM + re-init + interleaved) `PMM-OK`, T5/T6 green.

## Decision

Bitmap for Phase 2a. Throughput is tied where it matters; bitmap leads
free speed 2.3x, leaves 320 more frames usable, and costs 1/40th the
metadata — decisive on small boxes. Code is smaller (~110 vs ~150
lines), i.e. less audit surface.

## Honesty notes (not a universal claim)

- The API is order-0-only, so buddy's contiguous-block advantage is
  unusable today; absorption is full on both sides.
- When multi-order allocation arrives (2MB pages, DMA), this decision
  MUST be revisited with new measurements — buddy stays in-tree as the
  documented alternative, still passing every invariant.

## Consequences

- Guest links `pmm_bitmap.c` (`PMM_BACKEND_C` in `tools/nova.py`).
- Multi-zone arenas (regions beyond the largest) stay explicit future
  work; `pmm_init` documents the single-arena policy.
