# ADR-0045: PS/2 mouse — parser, cursor, click-to-focus (12e)

Status: accepted (Phase 12e). Date: 2026-10-05.

## Context

Windows are viewable and keyboard-manageable, but there is no
pointer. A PS/2 mouse (IRQ12, 3-byte packets, cursor +
click-to-focus) is the last input before widgets mean anything.

## Decision

- `kernel/input/mouse.c`: bounded 8042 init, pure packet decode
  (9-bit sign extension, overflow drop, bit3 desync rule),
  clamped cursor, button edges; left press hit-tests the topmost
  window and focuses + presents (stores only: IRQ-safe under any
  preemption, including mid-present). Cursor draws last in every
  present (nothing covers the pointer).
- The controller here is write-deaf (see below), so init is
  two-tier: `MOUSE-INIT` on reset proof, `MOUSE-ABSENT`
  (non-fatal, dormant: no IRQ, hidden cursor) on silence.
  Parser/click/hit/cursor logic selftests regardless
  (`MOUSE-OK`, real motion excepted); T20 replays the matrix
  plus click wiring against stub compositor calls.
- Shell/ABI: none (pointer queries are future; the desktop is
  operable by keyboard today).

## Evidence

Guest `MOUSE-ABSENT` + `MOUSE-OK`; `nova test` 20/20.

## Hard lessons

- Writes to 8042 ports 0x60/0x64 are black-holed under this
  VBox 7.2 setup (every output-producing command probed: 0x20,
  0xA9, 0xAB, 0xE0, 0xC0, keyboard 0xED -- all silent) while
  status reads are sane (0x1C). Device source (DevPS2) says
  those commands must answer, so this is environment behavior,
  not driver error. Design for dormancy, not death: prove
  everything provable, mark the boundary (motion needs a
  talking controller).
- Short polls for absent hardware: a live controller answers
  synchronously, so 5000 polls distinguish deaf from slow
  without the 30s stalls the first 100k-poll waits caused.
- Stale prints from combined call-in-check expressions: gcc
  hoisted `(unsigned long)dx` loads above the parse call they
  print (disassembly proved it: pre-call values reached the
  FAIL line while the verdict used fresh ones). Rule: call,
  then assert in separate statements -- especially in tests,
  where a lying print misdirects the whole investigation.
  (Also fixed: the neg1 vector wanted x-sign 0x38, not 0x28.)

## Consequences

- Motion path reviewed, not executed (no hand on the host);
  cursor + click activate wherever the 8042 talks.
- Next: widgets/toolKit on top of titles + pointer, or release.
