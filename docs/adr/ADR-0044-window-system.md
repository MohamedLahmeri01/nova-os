# ADR-0044: Interactive window system + desktop bar (12d)

Status: accepted (Phase 12d). Date: 2026-10-05.

## Context

12c left windows viewable but untouchable: no userspace path
could open, list, title, focus, or close them. 12d wires the
window system to userspace (keyboard-driven; no pointer without
a mouse driver) plus persistent desktop chrome.

## Decision

- Five syscalls (25-29: focus/open/stat/title/close), plain
  scalars except two struct-carrying calls sharing ONE header
  (`sdk/include/nova/types.h`, included by both sides; the
  kernel build gained `-I sdk/include` so layout drift breaks
  compilation instead of memory). `SYS_WINSTAT` doubles as a
  count probe `(NULL, 0)`; string copy truncates documentedly.
- Window ops are ungated in v0.1 (any local process arranges
  the shared console screen): per-window ownership + a GUI
  capability are recorded future work, same class as the 10c
  graft decision (foreign views opener-owned).
- Menu bar painted last in every present (always on top):
  "NOVA OS" + focused title. Demo geometry moved below the bar
  (nothing legible may live at y 0..12).
- Shell `focus`/`wins`/`winopen`/`winclose`/`wintitle` (decimal
  arg parsing, palette range-checked).
- `win_selftest` drives real dispatch with crafted frames (no
  current needed: handlers are proc-agnostic): empty focus,
  bad memory, bad ids, count probe, then direct-open +
  dispatched-focus + direct-stat + dispatched-close; prints
  `WIN-OK`, gated both lists. The demo moved after all
  selftests so the pool packs 0,1,2 deterministically.

## Evidence

Guest `WIN-OK`; screenshot shows menu bar + titled windows +
console body; `nova test` 19/19.

## Hard lessons

- Selftest ordering is global state management: the demo had
  to move after the tests, not the tests around the demo.
  Paint-last (demo) vs assert-first (tests) is a boot-layout
  rule worth stating.
- T19 pixel expectations live under the chrome: every y < 12
  assertion broke with the menu bar. Test points must document
  which layer owns them.

## Consequences

- Desktop shell v0.1 complete: keyboard window management +
  persistent chrome. Mouse driver, widgets, per-window
  ownership, live console mirror: future.
- Next: whatever the roadmap says after Phase 12 (release?).
