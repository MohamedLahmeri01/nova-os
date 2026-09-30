# ADR-0007: Security model — capabilities + sandboxing hooks

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Discretionary permissions alone do not contain compromised services;
full MAC/capability systems are large builds. Need least-privilege now
with room to grow.

## Decision

Baseline: UIDs/groups/permissions + capabilities + secure handles +
resource limits + sandboxing hooks, per the threat model. Capability
purity and MAC policies evolve only with justification; secure boot /
measured boot noted as future, not claimed.

## Consequences

- Every privileged interface gets the threat-model checklist review.
- Sandboxing must be testable (a sandboxed process demonstrably loses
  a right), not documentary.
