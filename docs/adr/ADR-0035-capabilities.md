# ADR-0035: Process capabilities + sandbox primitive (drop-only)

Status: accepted (Phase 10a). Date: 2026-10-04.

## Context

Every userspace process held ambient authority: any program could
transmit (ping), reconfigure the network (dhcp), or spawn
children. ADR-0007 requires least-privilege now, and demands
sandboxing be testable (a sandboxed process demonstrably loses a
right), not documentary. Phase 10 is sliced: 10a capabilities +
sandbox, 10b resource limits, 10c UIDs + file permissions, 10d
filesystem jailing.

## Decision

`struct process` gains `caps` + `uid` (default-deny: 0/nobody;
`spawn_image` inherits the spawner's; init is granted all/root at
adoption). Two rights in v0.1: `CAP_NET` (ping, dhcp) and
`CAP_SPAWN` (exec); gates run first in each handler (`-EPERM`,
before any argument work). `SYS_CAPS` observes (GET) or shrinks
(DROP, unknown bits refuse); no interface raises caps, so
sandboxes only tighten. Boot context without a current thread
allows (gates bind threads, not early selftests). Shell `caps` +
`dropcap net|spawn|all` demo the degraded session live.
`caps_selftest` proves the sandbox through real dispatch: a
capless scratch process (current thread swapped under `cli`)
gets EPERM on ping/dhcp/exec, then after a grant succeeds, then
after a NET drop refuses again; prints `CAPS-OK`, gated in
`run-vbox` + `panic-test`. Host T15 fuzzes the pure
allow/drop logic for monotonicity.

## Evidence

Guest `CAPS-OK`; `nova test` 15/15.

## Hard lessons

- `sched_enter` wrote `t->state` unconditionally: restoring a
  NULL saved-current (boot runs before adoption) faulted at the
  END of an otherwise passing test. Read the fault (objdump on
  the .pe: `movl $1,0x8(%eax)` with eax=0), then made NULL clear
  instead of faulting.
- Gate-first ordering matters twice: EPERM precedes validation
  (a capless exec with a bogus path still reports EPERM), and
  unknown rights are EINVAL (caller bug), not EPERM (denial).

## Consequences

- Init's children (shell, hi) inherit full caps; sandboxes are
  opt-in per session (`dropcap`) until 10c/10d add ownership
  and jails.
- Next: 10b resource limits (frames, fds, ticks, file size).
