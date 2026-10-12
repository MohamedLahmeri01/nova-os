# Proposed ADR — Real-time contract: measured bounds, two workload classes (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Strict-prio + 10-tick slices is an RT seed, not
an RT guarantee: no latency analysis, no priority
inheritance, IRQ work charged to whoever was
preempted. Options: (a) measured RT profile —
bounded interrupt-off sections inventoried,
latency histograms under load as gates, RT vs
best-effort classes separated; (b) hard-RT claim
(reject without certification evidence and
hardware analysis); (c) stay best-effort (reject
for D: abandons the secondary target).

## Trade-offs

(a) converts the scheduler from "strict" to
"strict with known worst cases" — the property
control buyers test. Costs: PI implementation,
per-path IRQ-off budgets enforced by measurement,
and saying NO to unbounded work in RT class.

## Prerequisites / evidence to decide

M2: worst-case IRQ-off inventory (every cli
section timed); latency histograms as gates;
PI design; certification target named (the bar
differs: industrial vs automotive vs medical).
