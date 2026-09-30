# ADR-0001: Hybrid/modular kernel architecture

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Microkernel purity minimizes TCB but adds IPC overhead and schedule
complexity; monolith maximizes performance but grows privileged code and
coupling. NOVA targets real hardware compat + security + contributor
scaling.

## Decision

Hybrid/modular: privilege only where it measurably pays (sched, mm,
interrupts, IPC primitives, syscall gate, security primitives, minimal
VFS iface, early drivers). Filesystems, net stack upper layers,
graphics, audio, packaging run isolated where practical behind stable
interfaces.

## Alternatives considered

- Pure microkernel (rejected: premature IPC/perf burden, small team).
- Monolithic (rejected: TCB growth, coupling).

## Consequences

- Must maintain strict interface boundaries + a service-isolation path.
- Scheduler/VFS/driver interfaces must be replaceable; review every
  proposed kernel addition against "does this need ring0?".
