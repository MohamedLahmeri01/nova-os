# Proposed ADR — Driver and DMA trust: policy now, isolation later (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Drivers are ring-0 with lifecycle = "called from
pm_main" (W3 OPEN); DMA writes anywhere (W5).
Options: (a) lifecycle + DMA policy first
(bind/probe/remove, owned buffers, IOMMU where
present), isolation later; (b) isolated drivers
now (separate address spaces / user-mode drivers);
(c) trust everything (reject: contradicts the
threat model the day a real NIC/DMA device ships).

## Trade-offs

(a) makes every future driver reviewable and
closes the cheap holes (BAR validation already
exists; extend to all DMA descriptors) without
the address-space machinery. (b) is the end
state for hostile devices, but premature before
M2 memory protections exist to build it on.

## Prerequisites / evidence to decide

M1 driver-lifecycle doc; IOMMU capability survey
of target boards; fault-injection harness for
descriptor lies (extend the drill pattern). No
new DMA device accepted without a policy entry.
