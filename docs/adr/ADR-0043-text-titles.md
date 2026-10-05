# ADR-0043: BIOS font + text + titled windows (12c)

Status: accepted (Phase 12c). Date: 2026-10-05.

## Context

Windows without titles are colored rects; a console without
text is a blue box. 12c adds text the honest way (real
letterforms, executed proofs) plus titled windows and a console
demo, still without input (no widgets fire without events;
no pointer without a mouse driver).

## Decision

- Glyphs come from the VGA BIOS 8x8 font (INT 43h vector at
  0x10C, copied from below 1MB at `font_init`): professional
  letterforms, zero hand-authoring. Structural validation
  (blank space + sane ink coverage) selects BIOS vs an outline-
  box fallback, logged as `FONT-SRC bios/fallback` (readable
  failure, never silent garbage). Out-of-range chars render '?'.
- `fb_char`/`fb_text` (transparent bg, 64-char cap) in the fb
  driver; `fb_selftest` proves ink-inside-cell +
  nothing-outside for 'A' under either font.
- `comp_title` (15 chars, black on the bar) + console demo:
  console window with 3 body lines, two titled alerts, focus
  cycle ending on the console. Title-ink scan in `comp_selftest`
  proves text on screen; screenshot proves legibility.
- Host T19 loads a synthetic X-font (`font_load` test seam)
  for exact-pixel text/title assertions; the timed demo never
  runs on host.

## Evidence

Guest `FONT-SRC bios`, `FB-OK`, `COMP-OK`; screenshot shows
titled console/alert/green windows with 3 body lines;
`nova test` 19/19.

## Hard lessons

- Shared test assets must work under every font: the title
  probe used "A", which is blank in the host synthetic font
  (T19 red until "X", inked everywhere). Test strings are
  interface contracts across font implementations.
- Validation heuristics need stated bounds (space blank, ink
  in [1000, 5000] of 8192 bits): tight enough to catch garbage,
  loose enough for any real 8x8 font.

## Consequences

- No widgets, no input path, no live console mirror (demo
  prints, serial stays the log); all future with the toolkit.
- Next: 12d window system completion + desktop shell (needs the
  input decision: keyboard-driven focus first, mouse later).
