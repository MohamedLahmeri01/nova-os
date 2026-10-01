# ADR-0015: Address spaces and user mode entry

Status: accepted (Phase 4b). Date: 2026-10-01.

## Context

Phase 4 needs privilege separation without building the whole syscall
layer: per-process page directories and one proven ring-3 round-trip.

## Decision

One PD per process: kernel low 1GB cloned (shared frames, never freed
per-PD), user half private above 2GB (`addrspace_*`; kernel-half maps
refused outright). GDT gains user code/data (DPL3) + 32-bit TSS; TSS.ESP0
per test. Entry via iret (SS/ESP/EFLAGS/CS/EIP), return via INT 0x81
(DPL3 gate) → CR3 restore → longjmp home. The probe is position-free
assembly (no calls/globals), verified by CS == 0x1B plus a magic word
through a shared page. User threads are NOT scheduler-integrated yet
(synchronous round-trip; Phase 5+).

## Evidence

Guest: `USER cs=0x1b magic-ok`, `USER-OK`; `nova test` 7/7 with
USER-OK in T5/T6 gates.

## Hard lessons

- iret loads SS/CS only: DS/ES/FS/GS keep supervisor selectors and
  fault (#GP) on first use at CPL3. Reload them in user entry.
- Kernel/user address confusion: identity writes use physical
  addresses, but the user ESP must be the VIRTUAL stack top (caught
  via err=5 protection fault forensics, not guessing).
- TSS.ESP0 stack must be mapped in the USER PD too (faults land while
  CR3 is still the user's); cloning the full low identity map covers
  this by construction.

## Consequences

- Syscalls (Phase 5) reuse the 0x81 path shape with real validation.
- User stacks/data stay RW for now; user-RO and W^X follow with demand
  paging evidence, not before.
