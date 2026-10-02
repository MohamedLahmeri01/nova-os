# ADR-0017: Syscall ABI v1 — INT 0x80, validation, six calls

Status: accepted (Phase 5). Date: 2026-10-01.

## Context

User threads need kernel services (yield, exit, output, identity)
without trusting user input. The 0x81 test-only gate is replaced by a
real ABI: EAX number, EBX/ECX/EDX args, negative -errno returns,
version query, table-driven dispatch (new versions add rows).

## Decision

INT 0x80, DPL3 gate. Six calls: yield, exit, print (capped 1024B),
getpid, gettid, version. Every pointer/length passes range checks
(bounds, wraparound, per-call caps) plus per-page present/user/
writable verification against live tables (shared guest/host logic;
guest walks tables, host fuzz uses a fake map). Failures return
errno, never panic. Counters assert every call type fired in tests.

## Evidence

Guest: negative print returns -EFAULT (not a fault), hellos print,
`SYSCALL y=61034 p=4 e=3`, `SYSCALL-OK`; `nova test` 8/8 with
SYSCALL-OK in T5/T6 gates. Host (T8): edge corpus + 20000 fuzz inputs
vs an independent oracle, `VERDICT: validation correct`.

## Hard lessons

- The C syscall argument is the pushed ESP *value* (= address of the
  flags slot), so the frame struct starts there — phantom retaddr
  fields shift every register read (found via 40-dword stack
  forensics, three converging proofs).
- TOCTOU is negligible single-core (no concurrent mapper) and
  documented for revisit with demand paging; dispatch (not yet) and
  full MMU-differential fuzzing are explicit future work.

## Consequences

- Handles, blocking calls, and multi-frame I/O stay Phase 5+ work.
- The 0x81 path, setjmp/longjmp scaffolding, and dead probes were
  removed (no dead code in the tree).
