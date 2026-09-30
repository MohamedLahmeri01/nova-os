# ADR-0005: IPC — handles with explicit ownership

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

IPC bugs (use-after-close, leaks, confused deputy, priority inversion)
dominate OS CVEs. Semantics must precede performance tuning.

## Decision

Message passing + shared memory + events + pipes/channels, all via
per-process handles carrying rights. Explicit ownership/lifetime;
close invalidates; no handle forgery across processes. Zero-copy paths
are optimizations, not the base contract.

## Consequences

- Every IPC object documents lifetime + rights transitions.
- Fuzz IPC parsers/handles from introduction, not later.
