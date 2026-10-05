# ADR-0042: Compositor — layers, focus, demo scene (12b)

Status: accepted (Phase 12b). Date: 2026-10-05.

## Context

The 12a framebuffer proves pixels; windows need ordering and
focus before widgets (12c+) mean anything. v0.1 scope: a
kernel compositor with a deterministic demo, no text (font
arrives with the toolkit), no input (keyboard focus cycling is
interactive future; the model + transitions prove now).

## Decision

- `kernel/gui/comp.c`: fixed pool of 8 windows, id order = z
  order (stable, pixel-testable), title bar + fill + border per
  window (2px white focused, 1px gray otherwise), full-repaint
  present (64KB stores, no damage tracking). Focus highlights
  only (no reorder yet). All geometry clips; bad ids refuse.
- `comp_demo`: three overlapping windows, two focus advances
  with `COMP-FOCUS id=` markers and tick delays; final screen
  is the proof (green front window white-bordered).
- `comp_selftest` prints `COMP-OK` (pool packing, ENOSPC,
  geometry/id discipline, focus wrap, exact top-color layering
  at (15,30), close-focus fallback, slot reuse, drain);
  gated in `run-vbox` + `panic-test`. Host T19 replays
  layering/focus/close against a stub VRAM byte array plus the
  selftest itself (the timed demo never runs on host: ticks
  stand still).

## Evidence

Guest `COMP-OK` + `COMP-FOCUS 0/1/2`; screenshot shows
blue < red < green with the white focus frame on green;
`nova test` 19/19.

## Hard lessons

- Assert exact colors, not nonzero pixels: `(15,30)` must be
  window 1's fill green, which pins title-bar height, border
  side, and z order in one read. The first draft asserted
  nonzero and proved almost nothing.
- Title-bar-aware test points: the same coordinate means
  different things under focus changes (border color follows
  focus). Fix the focus state before asserting pixels, in both
  guest and host tests.

## Consequences

- No raise-on-focus, no damage rects, no text, no input path;
  all documented futures for 12c+ (font first, then widgets,
  then a mouse driver before any pointer).
- Next: 12c window system + toolkit (needs the font decision).
