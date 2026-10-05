# ADR-0041: Mode-13h framebuffer + splash (12a)

Status: accepted (Phase 12a). Date: 2026-10-05.

## Context

Phase 12 needs pixels before windows: a framebuffer driver with
a selftest and a visible proof, without disturbing the serial
console or the boot budget. VGA text (8d) cannot survive the
mode switch, so the change also retires it cleanly.

## Decision

- `stage1` selects VGA mode 13h (320x200x256, byte VRAM at
  0xA0000) via one BIOS call right before protected mode
  (5 bytes; 500B total, build asserts <= 510). All real-mode
  work precedes it, so text-mode error paths still work.
- `kernel/drivers/fb.c`: direct VRAM pixels + 16-entry DAC
  palette + filled rects, all silently clipped. `fb_selftest`
  proves clear/readback/rect/clip/palette-DAC-round-trip,
  prints `FB-OK`; `fb_splash` leaves border + bars + blocks.
- VGA text leaves the boot path (its aperture is planar in
  graphics mode: cell readback *cannot* pass, which the first
  graphics boot proved by failing). Driver stays for serial's
  inert mirror; `VGA-OK` leaves both gate lists, `FB-OK` joins.
- Screenshot (`vbox-boot-proof.png`) is the visual proof:
  dark-blue field, white frame, RGB bars, W/Y/C blocks.

## Evidence

Guest `FB-OK`; screenshot verified pixel-correct; `nova test`
18/18.

## Hard lessons

- A failing selftest can be the proof: `VGA-FAIL cell` on the
  first graphics boot confirmed the mode switch (planar
  aperture) better than any log line. Read failures for what
  they exclude, not just what they assert.
- Retire, don't conditionalize: a mode flag threading through
  the text driver would preserve dead code paths. The honest
  move was deleting the boot call + gate, documented in the
  header.

## Consequences

- Serial is the console now (text mirror inert); the screen
  belongs to graphics from boot.
- Next: 12b compositor (layers + keyboard focus + demo scene).
