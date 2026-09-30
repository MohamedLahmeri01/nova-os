# ADR-0003: Memory model — explicit address spaces, deferred allocator choice

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Allocator families (bitmap vs buddy vs slab/slub hybrids) trade
fragmentation, latency, and complexity. Choosing without measurements
repeats classic OS mistakes.

## Decision

Fix the MODEL now (PMM frame allocator → VMM address-space objects →
kernel heap → user mappings; guard pages, NX, COW, W^X kernel from the
start), but DEFER the allocator family to Phase 2 behind benchmarks
(allocation/free/page-fault latency, fragmentation, TLB impact).

## Consequences

- Phase 2 delivers at least two allocator candidates with numbers.
- No demand paging / page cache until the base allocator is measured.
