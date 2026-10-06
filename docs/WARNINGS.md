# NOVA Warning Inventory (v0.4 Phase Q — normative baseline)

`nova lint` enforces zero *errors*. Warnings are counted, reported,
and each is classified here. Adding a new warning without an entry
here fails review. `-Werror` waits until this file is empty (not
close: empty).

## Guest build (`-m32 -ffreestanding -Os -Wall -Wextra`)

Exactly 1, tolerated:

1. `kernel/syscall/syscall.c:1869` `jail_selftest`: `struct
   syscall_frame f` set but not read (`-Wunused-but-set-variable`).
   Verdict: KEEP. The zeroed frame is defensive hygiene for a
   harness that arranges process state directly; deleting it
   saves nothing and weakens the next reader's confidence the
   frame starts clean. Revisit only if the harness changes.

Previously fixed to reach this baseline (do not regress):
virtio hex-print `?:` sign-change, irq vector compare cast,
smp AP_TRAMP duplicate defines, bench unused variable,
T21 `/*`-in-comment. Each fix was zero-behavior-change;
the pattern for future fixes is the same.

## Host test builds (`-O2 -Wall -Wextra`, 64-bit)

Host-only artifacts (guest builds never show them; they fire
on 64-bit pointer widths and mingw headers):

1. `SYS_OPEN redefined` (mingw `stdio.h` vs `syscall.h`, every
   test including both). Verdict: KEEP LOUD. The values differ
   (`_SYS_OPEN` vs 9u); silencing with `#ifndef` would make
   meaning include-order-dependent. No test uses bare
   `SYS_OPEN`; if one ever does, this warning is the alarm.
2. `-Wint-to-pointer-cast` in `validate.c` (copy helpers),
   `font.c` (BIOS-font read), `nova.h` wrappers (T23 build).
   All cast `uint32_t` addresses: exact on the 32-bit guest,
   lossy noise on the 64-bit host where the code never
   executes (host rigs stub or avoid those paths; T23 uses
   `-m32` precisely to keep them exact). Verdict: KEEP
   (guest-exact by construction).
3. `-Warray-bounds` on `font.c:40` (IVT read via variable).
   GCC's heuristic misfires on casted-integer dereference on
   host; guest lint is clean; the access is correct (below
   1MB, identity-mapped readable) and never executes on
   host (T19 uses the `font_load` seam). Verdict: KEEP,
   with this note as the tombstone.

## Policy

- New code: zero warnings on both builds, or an entry here
  with a reason before merge.
- `-Werror`: blocked on emptying this file (1 entry) plus a
  host strategy (either `-m32` for all host tests, making
  host warnings structurally impossible, or per-file
  waivers — decision deferred to the `-Werror` ADR).
- clang-format/tidy/cppcheck: absent on this host (silent
  installs forbidden); compiler warnings remain the
  enforced linter. Revisit when tooling lands.
