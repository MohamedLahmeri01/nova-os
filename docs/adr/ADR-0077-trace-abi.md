# ADR-0077: Trace user ABI — projected records, scoped visibility (v0.5 Phase B)

Status: accepted (v0.5). Date: 2026-10-06.

## Context

Phase B audited the trace ring (`docs/TRACE-ARCHITECTURE.md`):
sound single-executor design, no fetch ABI, several fields
that must never cross the boundary raw (notably FAULT CR2
and any future pointer-carrying event). Phase C needs the
shape decided before it codes, so the implementer isn't
re-deciding security policy mid-implementation.

## Decision

- **Fetch design: C (explicit projected records).**
  A (raw ring) rejected: exposes padding, future fields,
  and kernel-layout evolution. B (struct memcpy) rejected:
  no kernel struct is ABI-designed. D (one-at-a-time)
  acceptable fallback if snapshots prove awkward. E
  (bounded kernel snapshot under IF, then copy out) is
  the REQUIRED mechanism under either.
- **Visibility: owning-process-plus-system by default,**
  exact scope decided in Phase C implementation review
  (options: any process / owner-only / capability-gated;
  rationale recorded there). No wider scope without a
  new decision. Rationale for restraint: thread existence
  is a process-enumeration side channel, and tick streams
  reveal load shape.
- **Loss semantics: sequence numbers REQUIRED iff the ABI
  promises loss detection.** The current ring cannot
  detect loss; Phase C either adds per-emit seqnos or
  documents best-effort explicitly. No silent third option.
- **SMP model: per-CPU rings, merge at fetch.**
  Rationale: emit path must never serialize across CPUs
  (a global lock on every emit prices IRQ latency
  everywhere); per-CPU rings keep emit lock-free, merge
  by (tick, seq) at fetch time. Applicable if/when APs
  emit; BSP-only design stands until then.
- **Panic/selftest co-update rule:** any record-layout
  change updates the panic dump + selftest readers in the
  same commit (they are layout-coupled today).

## Evidence

Audit-only decision: evidence is the Phase B source audit
(`docs/TRACE-ARCHITECTURE.md` §§1–8); implementation
proof lands in Phase C (fetch + tests) and the SMP ADR
(per-CPU rings).

## Hard lessons

- Deciding the ABI shape BEFORE the fetch syscall exists
  is the correct order for once: every prior ABI grew
  organically and needed retroactive discipline (see
  SYS_* mirror twinning). Constraints first, code second.

## Consequences

- Phase C implements projection + snapshot + validation
  against this decision; deviation needs a supersession
  with cause, not convenience.
- FAULT CR2 never crosses raw (project to fault-class or
  drop — Phase C chooses, documents).
- No kernel struct is fetchable whole, ever, without its
  own ABI-design ADR.
