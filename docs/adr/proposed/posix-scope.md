# Proposed ADR — POSIX scope: named-subset profile, never blanket claims (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Libc is 9 functions; POSIX coverage ~0%. Options:
(a) named-subset profile (each call specified,
validated, tested; profile versioned with the
ABI); (b) full POSIX (reject: 1,000+ interfaces,
most meaningless on an appliance); (c) Linux-ABI
compat layer (defer until the syscall surface
freezes — compat on a moving ABI is a treadmill).

## Trade-offs

(a) lets real software (agents, supervisors,
telemetry) port with bounded work while the "is
it POSIX?" question stays answerable per call.
The discipline that protects it: the profile is
a gate (ported-app corpus, each app green), and
anything outside the profile is honestly absent.

## Prerequisites / evidence to decide

M1 ABI freeze policy; M2 isolation (new calls
without it repeat pre-jail history); three named
target apps chosen BEFORE the profile (profile
serves apps, not completeness).
