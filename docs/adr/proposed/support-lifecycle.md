# Proposed ADR — Support lifecycle: named support windows and patch cadence (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

No lifecycle exists: no supported-version
statement, no patch cadence, no EOL policy.
Production buyers (and certifiers) treat this as
a blocker, not paperwork. Options: (a) time-based
releases with named support windows + security-
only backports; (b) rolling release (reject for
appliances: fleet cannot chase a moving target);
(c) LTS-per-board (defer: multiplies maintenance
before the first product ships).

## Trade-offs

(a) forces the boring machinery early (branches,
backports, advisories) — that is the point. The
disclosure process (SECURITY.md, M1) is its
prerequisite; without it, "support window" is a
promise with no one home.

## Prerequisites / evidence to decide

Disclosure process created AND exercised (a
drill advisory end-to-end in M7); first board
named (support attaches to hardware + software
pairs, never software alone).
