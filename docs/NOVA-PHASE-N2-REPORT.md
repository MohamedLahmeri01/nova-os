# NOVA OS — Phase N.2 Report: Interactive Beta, Shell Usability & Diagnostics

## Verdict

**PASS.** Three tracks implemented, host-proven, guest-
proven on screen; 23/23 ×3 + gate; demo executed;
no commit, no push.

## Starting / ending HEAD

- `0b5da64` → `0b5da64` (branch `main`, working tree).
- Baseline `59ea07b` ancestor: confirmed, intact.

## Initial working-tree state

Phase N code (pm_main + sh exit) + beta docs +
prior audits + screenshot, VM poweroff. No new
user changes since N.1.

## Feature tracks selected and rationale

- A — Discoverable shell (highest value: every
  session starts at `help`; risk lowest: strings
  + one table, no dispatch changes).
- B — `!!` repeat-last (workflow value for
  repeated `stat`/`ticks` watches; 128B static,
  no kbd changes — arrow-key history rejected:
  E0 scancodes never reach the shell, and driver
  surgery is out of scope).
- C — `uptime` + `about` (diagnostics from real
  sources only: ticks@100Hz nominal, build
  version string, ABI int; `tasks`/`cpu` refused:
  no enumeration ABI, no CPUID exposure).

## Actual code changes (userspace only)

- `userspace/init/sh.c` (+~110): `sh_cmds[]`
  37-entry table {name,desc,usage,bare_usage};
  `sh_lookup`; `help [cmd]` (compact list fits
  25 rows; per-command desc+usage); `!!` hook +
  128B static history (expansion never stored,
  no recursion); `sh_uptime` (honest nominal
  units, errno on error); `sh_about` (version/
  target/ABI, errno on error); dispatch branches
  for `help` (exact→prefix guard, same unknown
  output), `uptime`, `about`. No guard, limit,
  or dispatch-order changes otherwise.
- `tests/unit/test_shell.c` (+~90): TICKS=12345
  + VERSION=1 stub successes (success paths
  covered); `nova_exit` no-return redirect to a
  recording stub (test-only macro); empty-history
  test; 37-name dispatch-reachability; 16-name
  bare-usage; help variants; `!!` repeat;
  uptime/about content asserts; exit-once assert.
- Debug drivers (`build/dbg*.c`, binaries) created
  during diagnosis and DELETED (verified gone).

## The exit-hang diagnosis (worth recording)

New bare-`exit` test hung the host binary: root
cause is `nova_exit()`'s `for(;;)` in `nova.h`
(correct on guest — the thread dies; infinite on
host stub). Old suite never ran bare `exit`
("exit now" ≠ `exit`), so the landmine predated
N.2. Fixed in-harness (redirect macro), production
untouched, gdb-bisected with marker breakpoints.
The test earned its keep: it found a real host/
guest divergence on day one.

## Commands implemented or improved

New: `uptime`, `about`, `!!`, `help <cmd>`.
Improved: `help` (single-sourced from dispatch).
Unchanged behavior: all 34 prior commands
(incl. `help me` → same unknown text as before).

## Automated tests added

T23 gains: empty-history, 37× dispatch-real,
16× bare-usage, 4× help-shape, `!!` repeat,
uptime/about content, exit-once. Fuzz/corpus
unchanged (4000 lines still green around the new
branches, incl. exact-`!!` fuzz hits exercising
expansion).

## Full suite results

23/23 ×3 consecutive (official harness, fresh
builds) + `ci/gate.py` PASS + lint clean.
Zero failures in the N.2 campaign.

## Build / image / CI / panic / emulator

- build ok (userspace rebuilt), image attached.
- panic-test: covered via in-suite T6 in all 3 runs
  (green); no separate standalone runs this phase
  (no kernel/timer/irq change to justify more).
- Emulator demo: fresh boot + `help`, `help cat`,
  `uptime`, `about`, `!!`-repeats-`about`, all
  screenshot-proven on the display (5 shots).
  (Two mistyped injections also proved unknown-
  command + backspace paths stay clean.)

## Interactive demonstration outcome

BETA-DEMO.md extended (step 4b) and executed:
every new feature typed for real, every output
observed on screen. Exit guidance + poweroff
unchanged.

## Known issues and deferred features

- Arrow-key history: impossible without kbd
  changes (E0 dropped) — `!!` is the explicit
  alternative, documented in CLI.md.
- `tasks`/`cpu` diagnostics: refused honestly
  (no enumeration ABI / no CPUID syscall).
- Word-wrap splits words at col 40 (cosmetic,
  documented, no fix attempted).
- External ping/DNS, soak: carried over unchanged.
- No third track started; no TCP/crypto/FS work.

## Complete list of changed files

- M `userspace/init/sh.c`, `tests/unit/test_shell.c`,
  `docs/CLI.md`, `docs/BETA-SCOPE.md`,
  `docs/BETA-DEMO.md`, `CHANGELOG.md`.
- New: `docs/NOVA-PHASE-N2-REPORT.md` (this file).
- Deleted after use: `build/dbgdrv.c`, `build/dbg2.c`,
  `build/dbg.gdb` (+binaries) — verified absent.
- Untouched: kernel/, all prior reports/ADRs,
  manifest, user screenshot, everything else.

## Baseline and user changes intact

`59ea07b` history untouched; no reset/clean/stash;
no destructive tests; disposable VDIs only.

## Reproducing the beta demo

```text
python tools/nova.py build
python tools/nova.py image
(start NOVA-OS-CONSTRAINED headless; click to capture)
help | help cat | uptime | about | stat | !!
python tools/nova.py test     # 23/23
```

## Confirmation

No commit. No push. No tag. Full diff reviewed
(read every hunk; hunks listed above are all).
