# Proposed ADR — OTA and rollback: A/B with power-loss-safe state machine (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

No update mechanism exists (the dominant future
threat per THREAT-MODEL P8). Options: (a) A/B
partitions + atomic slot flip + verified boot of
the new slot, failed slot retained for rollback;
(b) single-partition journaled apply (smaller
media, harder power-loss proof); (c) RAM-only
updates (reject for product: no persistence).

## Trade-offs

(a) costs flash space but makes "interrupted
update" a tested state, not a brick: every power-
cut point maps to a bootable slot. Requires the
M4 crash-consistency story first (an updater on a
filesystem that cannot survive a crash cannot be
proven). Downgrade protection (anti-rollback
version rules) is part of this ADR, not an
afterthought.

## Prerequisites / evidence to decide

Signing pipeline + trust anchors (M3/M6); ten
hostile-update scenarios defined up front
(hostile server, corrupt package, cut at every
state transition, downgrade attempt); hardware
power-cut rig for M6 exit.
