# ADR-0053: Widget toolkit — labels, buttons, dialog (14d)

Status: accepted (Phase 14d). Date: 2026-10-06.

## Context

Windows had titles and paint callbacks but no controls:
no label primitive, no button frame, no shared geometry
query. Inputs don't exist yet, so interaction is out of
scope -- but a render-only toolkit plus a demo dialog is
separable, committable, and prerequisite to dialogs,
menus, and file pickers.

## Decision

- `kernel/gui/widget.h/.c`: `wid_label` (font text at
  window-relative offset) and `wid_button` (gray face,
  white top/left bevel, dark bottom/right, centered label)
  over `comp_geom` (id -> x/y/w/h, -EINVAL on dead ids).
  All drawing clips through fb primitives; NULL/min-size
  refuse before touching pixels.
- `wid_selftest` (guest, `WID-OK`): bad ids/NULLs refuse,
  scratch window inks label cell, exact bevel pixels +
  face + label ink asserted, window closed (pool drained).
- Demo gains a 4th window: teal `about` dialog with three
  labels (`NOVA OS` / `ABI v1` / `21/21 OK`) + `OK` button,
  drawn window-relative every frame (dogfooding), focus
  cycle extended 0,1,2,3,0.
- Host `T19` links/runs `wid_selftest` against the same
  stub VRAM + cursor rig (cursor parked bottom-right).

## Evidence

Guest `WID-OK`; focus log 0,1,2,3,0; screenshot shows
teal dialog with labels + beveled OK button; `nova test`
21/21.

## Hard lessons

- Selftest scratch geometry must avoid text the earlier
  test painted: the T19 `X...` row lives at y 100..108, so
  the scratch window went to y 150 (not beside the demo's
  old green slot).
- Min-size guard (w<16/h<14) isn't cosmetic: a narrower
  face can't fit bevel + 1-char label, and centered-text
  math underflows unsigned -- refuse, don't clip art.
- `comp_geom` with NULL out-pointers returns geometry
  status only; the label path passes NULL w/h because it
  needs origin alone. One accessor, two arities, no
  second helper.
- Host rig fonts are minimal by design: T19's synthetic
  font inked only 'X'/'?', so the selftest's "OK" label
  drew zero pixels on host while passing in-guest (BIOS
  font). Fix belongs in the rig (box 'O' + bar 'K'
  glyphs), not the selftest -- guest/host must run the
  identical assertion.

## Consequences

- Controls render; none respond (no input events yet).
- Next: input events (mouse click routing, focus-by-click),
  then press states; blocking RX (wait queues) still queued.
