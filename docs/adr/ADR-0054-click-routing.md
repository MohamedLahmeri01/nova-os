# ADR-0054: Shared click routing — comp_click (14e-i)

Status: accepted (Phase 14e, step i). Date: 2026-10-06.

## Context

Two click paths existed side by side: the mouse press
edge inlined hit + focus + present, while synthesized
clicks (tests, demo, future shell/scripts) had no path at
all. Any divergence between them would rot silently --
mouse hardware is dormant on this VM (MOUSE-ABSENT), so
the hardware path only ever executes via crafted bytes.

## Decision

- `comp_click(x, y)` (comp.c): `comp_hit` + focus +
  present + `CLICK id=` log, -1 silent on miss. IRQ-safe
  by construction (stores + full repaint, single core) --
  the stale "IRQ handlers never compose" comment goes.
- `mouse_feed` press edge calls `comp_click` (behavior
  unchanged, mechanism unified). `MOUSE-OK` now proves
  the shared path, not a private inline copy.
- `comp_selftest` gains click vectors on a scratch
  overlap pair: back-only hits back, overlap hits front,
  bar/off-screen/empty-pool miss with focus unchanged
  (`CLICK-OK` gate).
- Demo synthesizes `comp_click(233, 175)` (OK button
  center) mid-cycle: `CLICK id=3` on serial every boot,
  hardware or not; cycle still ends on console.

## Evidence

Guest `CLICK id=0/1`, `CLICK-OK`, `MOUSE-OK`,
`CLICK id=3` in the focus sequence; final screenshot
unchanged (console focused, dialog intact).

## Hard lessons

- Click coordinates need no validation: `comp_hit` only
  compares (never arithmetics on) the point, and paint
  clips -- (400,300) is safely a miss, not a hazard.
- Silence on miss is a feature: with live hardware,
  misses are routine pointer motion, not events.
- Unifying paths breaks the neighbors: T20 links
  `mouse.c` against stub compositor functions, so the new
  `comp_click` symbol needed a stub that mirrors the real
  semantics (hit -> focus + present, silent miss) -- and
  the press-edge assertions now prove routing *through*
  the shared path, which is stronger than the old
  focus-directly checks.

## Consequences

- Clicks reach windows, not widgets: which control was
  hit (button bounds, press states) is the next step.
- No user-space click surface yet (`SYS_CLICK`/shell
  `click` deferred until widget routing exists).
