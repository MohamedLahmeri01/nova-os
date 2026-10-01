# ADR-0016: Priority scheduling, SMP bring-up, scheduled user threads

Status: accepted (Phase 4c). Date: 2026-10-01.

## Context

Phase 4 needs scheduling depth and multiprocessor reality without
building the whole OS: strict priorities, AP bring-up, and user
threads under the scheduler.

## Decision

Strict 32-level priorities with round-robin inside each level
(replacing RR behind the same policy ops — the swap itself proves
replaceability); main/idle thread at 0. SMP via MP-table enumeration
(checksummed; graceful single-CPU fallback) and INIT/SIPI bring-up of
a position-independent trampoline; APs self-check (LAPIC ID), print,
and park (no AP scheduling yet). User threads enter through a
trampoline frame consumed by plain ctx_switch (uniform with every
other resume — no layout branches), exit via INT 0x81 dispatch
(scheduled path switches, sync path longjmps), with TSS.ESP0 reset on
every switch plus a permanent fallback stack. Overlap proofs with
recorded windows (kernel tick windows, user progress counters).

## Evidence

Guest: `SCHED prio=1`, `AP 1 online`, `SMP-OK`, `USER-SCHED-OK`;
`nova test` 7/7 with SMP-OK + USER-SCHED-OK in T5/T6 gates.

## Hard lessons

- mingw `-m32` adds a `_` prefix to C symbols; asm labels are
  literal. C must declare `foo` to match asm `_foo`. Writing `_foo`
  in C yields `__foo` and fails with misleading "undefined reference"
  (TOOLCHAIN.md now states the rule; cost: a long forensics detour
  through objdump/nm/ld internals that all looked innocent).
- Relocated trampolines: relative calls break across the move (target
  stays, code moves); absolute indirect calls survive. 16-bit absolute
  addressing truncates above 64K — real-mode trampoline code may use
  numeric literals only (verified by disassembling the built bytes).
- MP CPU flags live at entry byte 3, not 2 (byte 2 is the APIC
  version, constant 0x14 across CPUs — a plausible-looking wrong
  answer that yields zero CPUs).
- Strict priority starves observers: overlap sampling needs a
  same-level observer thread, and main (prio 0) can only wait.
- The 0x81 return path must restore IF (longjmp preserves flags);
  otherwise the timer dies silently downstream.
- Order matters: fill-then-copy for trampoline data (not copy-fill).

## Consequences

- No aging (strict starvation possible by design), no AP scheduling,
  no migration/balancing, no user yield (exit-only user threads):
  all explicit Phase 4d/5 work, not oversights.
- TSS.ESP0 invariant (always valid) is now enforced in code, not just
  documented.
