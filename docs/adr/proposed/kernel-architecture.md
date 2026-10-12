# Proposed ADR — Kernel architecture: retain the monolith (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

CONFLICT NOTE: ADR-0001 records a hybrid/modular
direction. The v0.5 tree is a monolith (FS, net,
drivers in ring 0; gap W3). This proposal asks to
ratify the monolith as strategy, which revises
ADR-0001's direction — explicit review required;
do not implement against this file until ratified.

## Context

Options: (a) retain monolith, earn isolation
incrementally; (b) microkernel/hybrid rewrite;
(c) Unikernel-style single-address-space (reject:
kills the user/kernel boundary v0.5 proved).

## Trade-offs

(a) keeps the 50-marker proof corpus, the audited
lock/IF discipline, and the review culture; pays
W3 (drivers in ring 0) until late. (b) buys
isolation architecture early but discards every
v0.5 proof and re-opens all audits with a team
sized for one tree, not two. Evidence for (a):
THREX/XSUB/FSSTRESS/NETSTRESS all assume current
structure; no measured isolation failure demands
(b) today.

## Prerequisites / evidence to decide

Comparative fault-injection study (same bug class
in monolith vs isolated prototype) OR a customer/
certification requirement naming isolation. Until
either exists, (a) holds by default and M2/M4
proceed inside it.

## Reassessment (Darwin-inspired review, still unratified)

The four-architecture comparison (DARWIN-
INSPIRED-ARCHITECTURE-REVIEW.md §4) REFINES this
proposal: retain the foundation, evolve toward a
MODULAR monolith — one privilege domain, but
explicit tested boundaries in fixed order (arch
extraction → HAL → driver lifecycle/registry →
service seams; userspace only for NEW crash-safe
services with measured IPC budgets). No hybrid/
microkernel transition: no requirement names one
and the corpus cost is concrete. Boundaries too
immature to stabilize now: scheduler policy
interface, VFS op granularity, any driver ABI.
Revisit only on: certification demanding
isolation, a fault-injection study showing
boundary value, or third-party-driver product
need. Original text above unchanged (history).
