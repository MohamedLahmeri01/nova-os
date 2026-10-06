# ADR-0065: Kernel language — C-first, Rust deferred to components (Phase A)

Status: accepted (v0.3 Phase A). Date: 2026-10-06.
Supersedes: ADR-0002 (Rust-first) on the implementation-language point only.

## Context

ADR-0002 (Phase 0) chose Rust-first for generic kernel logic with
isolated C/asm. Six months of implementation later the tree holds
0 `.rs` files, 0 `Cargo.toml` files, and a 100% C kernel (~130KB
binary) with isolated GAS assembly (boot, context switch, IRQ
entry) — i.e. the exact shape ADR-0002 allowed only as a "C
fallback path", become permanent without a decision. The v0.2.0
audit (P0-2) flagged the contradiction. Meanwhile rustc 1.98.1
sits installed on this host, unused; bare-metal targets are not
installed, and host policy forbids silent installs.

## Decision

- **Option A (chosen): C-first kernel.** Record what is true:
  C for generic kernel logic, isolated C/asm only for boot
  trampolines, context switch, and interrupt entry (unchanged
  in practice; changed on paper). No rewrite of working code
  to satisfy the old ADR (master rule: rewrites need evidence,
  and none exists — see below).
- Future Rust is **components-only, ADR-gated**: a Rust crate
  may enter as a userspace program, a host tool, or an
  isolated driver only with its own ADR (build integration,
  `unsafe` policy, audit story). No tree-wide migration
  without measured memory-safety failures to justify it.
- Compensating controls (all in-tree, all enforced by the
  suite — the safety case for staying in C): `-Wall -Wextra`
  clean builds (warnings fail host-test compiles), syscall
  validation + 20k-input fuzz (T8), FS/net parser fuzz +
  corruption injection (T9–T14, T21), stack canaries checked
  every switch-in, heap frame tags + magic + always-on
  double-free walk, fail-closed validation before use,
  deterministic selftests that panic (never limp) on
  violation. Known gap, stated not hidden: no NX/W^X on
  32-bit non-PAE (Phase C investigates what the hardware
  actually allows).

## Evidence

Inventory: `*.rs` count 0 (repo-wide search), `Cargo.toml`
count 0; build log shows `-m32 -ffreestanding` gcc-only
pipeline; defect history across 63 ADRs shows logic/IRQ/
concurrency bugs caught by tests — zero recorded incidents
of a class Rust's borrow checker would have prevented
(buffer overruns caught instead by tags/magic/fuzz).
Toolchain: rustc present but bare-metal targets absent;
installing them silently would violate host policy.

## Hard lessons

- An aspirational ADR with no migration owner and no
  first-Rust-crate milestone is a wish, not a decision:
  ADR-0002 survived six months unimplemented because
  nothing forced the choice. Decisions need either an
  implementation step or a sunset review.
- "Preferred language" and "implemented language" must
  be reconciled at every release checkpoint; the release
  checklist now implies it (this ADR is the first
  reconciliation).

## Consequences

- ADR-0002's `unsafe`-invariant process is moot (no Rust
  to apply it to); the C compensating controls above are
  the review bar instead. A future Rust ADR reinstates
  per-crate rules.
- Contributors write C (+ GAS where isolated); reviewer
  guidance lives in CONTRIBUTING-adjacent practice, not in
  a contradicted ADR.
- The C↔SDK header sharing (`-I sdk/include` breaking
  drift at compile time) stays the cross-boundary
  integrity mechanism.
