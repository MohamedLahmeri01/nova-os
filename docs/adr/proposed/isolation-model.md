# Proposed ADR — Workload isolation: processes + caps + quotas first, containers later (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Isolation today = processes + caps + jails +
rlimits (proven). Untrusted/multitenant workloads
need more. Options: (a) deepen the current model
(quotas, namespaces-grade separation, supervisor-
enforced policy) and stop there for the first
product; (b) containers (needs (a) first anyway +
image/format/runtime ecosystem); (c) VMs (reject:
needs a hypervisor NOVA is not).

## Trade-offs

(a) is proportionate to an appliance running
first-party agents; its proofs extend THREX/XSUB
patterns. (b) buys ecosystem compatibility at the
cost of a runtime, image format, and registry
trust story — a second product's problem.

## Prerequisites / evidence to decide

M2 memory protections; per-object quota proofs;
escape-test battery (jail/namespace breakouts as
gates, red-teamed). "Isolated" is claimed only
with failing escape tests on record.
