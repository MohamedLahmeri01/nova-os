# ADR-0056: User-space click — SYS_WINCLICK + shell click (14e-iii)

Status: accepted (Phase 14e, step iii). Date: 2026-10-06.

## Context

Clicks were kernel-only: IRQ edges, selftests, and the
demo's synthesized call. Scripts and future apps had no
surface -- `focus` cycles blindly, `wins` only reports.
Coordinates need no validation (hit compares, paint
clips), so the syscall is a thin pass-through by design.

## Decision

- `SYS_WINCLICK 34` (`SYS_MAX 34`): `do_winclick(x, y)`
  -> `comp_click`, returns hit id / -1. Table entry
  `winclick`; ABI.md regenerated (T17 freshness holds).
- `win_selftest` dispatch vectors (run in-guest, under
  `WIN-OK`): empty-pool miss -1, hit (20,160) -> 0 with
  focus pinned, bar miss (20,4) -> -1 focus unchanged.
- SDK `nova_winclick(x, y)` + shell `click X Y`
  (two-uint parse, `usage: click x y` discipline,
  `click id=N` / `click: no window` replies).

## Evidence

Guest `CLICK id=0` (dispatch hit), `WIN-OK`, demo
`CLICK id=3`; ABI table row 34; `nova test` 21/21.

## Hard lessons

- The SDK vendors its own `SYS_*` mirror (not an
  include): adding the kernel number without the SDK
  twin breaks the userspace build with `undeclared`.
  The failure is loud (compile error), not silent --
  but only because a wrapper referenced it. An unused
  number would drift quietly; wrappers must land in the
  same commit as numbers.
- Shell dispatch has no exact-match table (prefix
  chains): `click` slots safely today (`clear` differs
  at the third char), but each new command needs the
  prefix-conflict check by hand.

## Consequences

- Clicks are scriptable; widgets still don't track
  press/release pairing (no event owner).
- 14e input work complete (routing + states + surface).
  Next: blocking RX (wait queues).
