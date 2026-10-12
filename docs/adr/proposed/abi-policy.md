# Proposed ADR — ABI policy: freeze with versioning before ecosystem (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

38 syscalls, INT 0x80, errno model, SDK mirrors —
stable in practice since v0.4 but promised to
nothing (ABI-STRATEGY says no promise before 1.0;
master-spec 1.0 bar referenced but not in-tree).
Options: (a) freeze v1 now with per-namespace
versions + deprecation policy, all future change
additive; (b) keep evolving freely until M8
(reject: every consumer writes against sand);
(c) version per-call from today without freezing
(acceptable interim: cheap, honest).

## Trade-offs

(a)/(c) cost discipline (CHANGELOG migration
notes, compat shims in tests) and buy the only
thing an ecosystem needs: a floor that does not
move. The 32/64 transparency rules must become
lint-enforced at freeze time, not folklore.

## Prerequisites / evidence to decide

Audit of every struct crossing the boundary for
width/padding discipline; syscall-table single-
source already exists (keep); 1.0 evidence bar
written down (currently cited, not present).
