# Proposed ADR — Platform sequencing: x86-64 product, ARM64 second, x86-32 validation-only (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Guest is x86-32 (BIOS/VBox). Product hardware is
64-bit; IoT is ARM. Options: (a) x86-64 first
(MMUs, drivers, and tooling closest to current),
then ARM64; (b) ARM64 immediately (market pull,
maximum rework); (c) stay 32-bit (reject:
memory ceiling, missing security features, no
ecosystem).

## Trade-offs

(a) reuses the VBox rig, the PCI/virtio drivers,
and the team's x86 knowledge; ARM64 follows once
abstractions are proven by carrying x86 with zero
behavioral delta. (b) is defensible only with a
named board + buyer in hand. x86-32 remains the
cheap deterministic reference (keep booting it in
CI) but must not accept product features after M5.

## Prerequisites / evidence to decide

M1 arch/timer/irq abstractions with identical
serial bytes on x86; named first board (product
decision, not taste); 64-bit ABI padding audit.
x86-32 end-of-support named here, not discovered
later.
