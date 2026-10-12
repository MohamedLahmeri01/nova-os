# Proposed ADR — First production CPU architecture: x86-64 (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

32-bit guest cannot carry NX/SMAP/PCID/IOMMU
product stories; ARM64 has the market but all
the new work (GIC, timers, DT, board). Options:
(a) x86-64 first (long mode, 4-level tables,
LAPIC, syscall insn, closest to today's tree/
rig/drivers), ARM64 second; (b) ARM64 now
(reject without a named board + buyer: maximum
rework, minimum validation); (c) stay 32-bit
(reject: ceilings documented in the review §6).

## Trade-offs

(a) unblocks memory protections and SMP on known
ground; defers market access. The risk it
manages is rework: abstractions proven by
carrying x86 with zero delta are the same ones
ARM64 needs. x86-32 stays as validation CI with
named end-of-support (see platform-sequencing).

## Prerequisites / evidence to decide

M1 abstractions + portability test green;
long-mode boot design (own loader step);
pagetable/memory-layout review; 64-bit ABI
padding audit; toolchain (x86-64 cross) pinned
like today's mingw.
