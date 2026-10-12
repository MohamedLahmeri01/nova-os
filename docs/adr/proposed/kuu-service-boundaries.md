# Proposed ADR — Kernel/userspace service boundaries (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Proven kernel services (sched, VM, VFS iface,
sockets, caps) stay; everything else is a
placement question. Options: (a) stay-by-default
+ move-out-only-new-services (update agent,
telemetry, provisioning, DHCP/DNS policy,
supervision), each with IPC budget + crash-
survival proof; (b) wholesale service extraction
(reject: IPC tax + rewrite without a fault case);
(c) nothing ever leaves (reject: update/telemetry
in ring 0 grows TCB for no reason).

## Trade-offs

(a) bounds the TCB where growth is happening
while the proven core stays put and gated. Each
move needs: fault-injection justification,
measured IPC cost, RAM/footprint budget, and an
OOM-survival story (supervisor restarts it, box
does not brick).

## Prerequisites / evidence to decide

M2 IPC primitives with waiter-grade proofs;
supervisor design (dependency-ordered starts);
per-service budgets in the matrix before code.
