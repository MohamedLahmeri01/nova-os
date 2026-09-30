# ADR-0002: Kernel language — Rust-first with isolated C/asm

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Candidates: C (universal, mature, unsafe everywhere), C++ (richer
abstractions, heavier runtime/ABI surface), Rust (memory safety,
no GC, mature bare-metal story, harder bootstrapping/interrupt/ASM).

Host evidence: rustc 1.98.1 present; bare-metal targets available but
not installed; mingw-w64 gcc 15.2.0 + MSVC present for C/asm shims.

## Decision

Rust-first for generic kernel logic; small, isolated C/assembly only
for: boot trampolines, context switch, interrupt entry, and whatever
has no stable Rust equivalent. Every `unsafe` block documents its
safety invariant; `unsafe` is a review blocker without one.

## Alternatives considered

- C-only (rejected: forfeits whole-class memory-safety wins).
- C++-first (rejected: larger implicit runtime, weaker safety payoff).

## Consequences

- Phase 1 must add `x86_64-unknown-none` (+ `i686-unknown-none` for
  x86-32) via an approved, user-confirmed install step — never silent.
- Keep a C fallback path for early boot until the Rust entry is proven.
- Pin toolchain versions; record in SBOM/build metadata.
