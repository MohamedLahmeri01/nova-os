# ADR-0055: Button hit + pressed look — stateless (14e-ii)

Status: accepted (Phase 14e, step ii). Date: 2026-10-06.

## Context

Clicks reached windows (`comp_click`) but not controls:
no way to ask whether a point hits a button, no
pressed rendering. The toolkit is deliberately stateless
(no widget registry), so press *tracking* has nowhere to
live yet -- but hit-testing and the down look are pure
functions of geometry the caller already knows.

## Decision

- `wid_hit(id, dx, dy, w, h, px, py)` (widget.c): 1/0/
  -EINVAL. Edges match `comp_hit` (left/top in,
  right/bottom out) so window-hit and control-hit agree
  on borders.
- `wid_button_down` shares a `button_draw(..., pressed)`
  painter with `wid_button` (no geometry duplication):
  inverted bevel + label nudged +1,+1. Same validation.
- `wid_selftest` gains press vectors (`PRESS-OK` gate):
  discipline on both new calls, five hit-edge probes,
  exact inverted-bevel pixels, label ink in the shifted
  box. Up-state asserts run first (down overwrites).
- Demo draws the OK button down on the click frame
  (press feedback), released on the return to console;
  final screenshot unchanged by design.

## Evidence

Guest `PRESS-OK` + `WID-OK`; host T19 runs the identical
vectors (O/K rig glyphs cover the shifted label);
`nova test` 21/21.

## Hard lessons

- Order selftest paints up-before-down: the two states
  share pixels, so assert the up bevel before the down
  render overwrites it. Same class of bug as
  paint-then-probe in compositor tests.
- The shifted label stays inside the unshifted scan box
  (+1,+1 of an 8px glyph in a 55px box): one scan region
  proves both states, no second geometry to rot.

## Consequences

- Controls report hits and render pressed; nobody
  tracks press/release pairing yet (needs an event
  owner -- compositor registry or input queue).
- Next: user-space click surface (`SYS_CLICK`/shell
  `click`), then blocking RX (wait queues).
