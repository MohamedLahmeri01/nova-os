# NOVA OS — Testing Strategy (Phase 0)

Status: plan. Nothing below passes yet because nothing is implemented.

## Pyramid

1. Unit (host-side, fast, no VM): allocators, parsers, data structures,
   syscall table invariants, boot-info validators. Run with
   `python tools/nova.py test` (currently stubs).
2. Integration (VirtualBox-gated): boot marker over serial, memory-map
   sanity, panic-handler output, later: process spawn, VFS, net
   loopback. Each boots an ISOLATED `NOVA-OS-*` VM; existing VMs never
   touched; artifacts under `build/` + `images/` (gitignored).
3. Stress (nightly, later): high process/thread counts, memory/IO
   pressure, SMP, corruption injection.
4. Fuzz (from Phase 5 for syscalls; earlier for boot-info/filesystem
   parsers): corpus checked in under `tests/`.

## Gates

- Every PR: format + lint + compile + unit + (once Phase 1 lands)
  VBox boot test. Failure blocks merge on protected branches.
- Performance: no "optimized" claim without before/after numbers and
  the benchmark recipe checked in under `benchmarks/`.

## Conventions

- Tests are hermetic, deterministic, and fast by default; VM tests are
  explicitly marked and skipped when VirtualBox is unavailable.
- A test failure prints: what ran, expected vs actual, serial/log tail,
  and the exact `nova.py` invocation to reproduce.
- Coverage is tracked as a signal, never as a target; missing coverage
  on privileged code is a review blocker.
