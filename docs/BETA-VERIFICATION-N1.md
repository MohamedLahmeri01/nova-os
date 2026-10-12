# NOVA OS — Beta Verification N.1 (executed 2026-10-09)

## 1. Starting / ending HEAD

- `0b5da64` → `0b5da64` (branch `main`).
- Baseline `59ea07b` ancestor: confirmed.
- Initial tree: Phase N code changes present
  (`kernel/pm_main.c`, `userspace/init/sh.c`),
  beta docs present, prior-audit docs + user
  screenshot intact. VM poweroff (safe).

## 2. Initial working-tree status

`M CHANGELOG.md, docs/CLI.md, docs/THREAT-MODEL.md,
kernel/pm_main.c, userspace/init/sh.c` + untracked
audit/beta docs + `docs/adr/proposed/` + screenshot.
No new user changes since Phase N.

## 3. Exact commands and outcomes

| # | Command | Exit | Outcome |
| 1 | `nova.py lint` | 0 | clean (1 tolerated warning) |
| 2 | `nova.py build` | 0 | kernel 162068B, blobs embedded |
| 3 | `nova.py image` | 0 | vdi attached, serial routed |
| 4 | `panic-test` | 0 | PASS ~17 s, INJECT-FAULT ×1 contiguous |
| 5 | `panic-test` | 0 | PASS ~17 s |
| 6 | `test` (suite 1) | 0 | 23/23 |
| 7 | `test` (suite 2) | 0 | 23/23 |
| 8 | `test` (suite 3) | 0 | 23/23 |
| 9 | `ci/gate.py` | 0 | PASS |
| 10 | demo boot + `stat` | 0 | live values on screen (screenshot) |
| 11 | demo `cat /nope` + `exit` | 0 | clean error + guidance (screenshot) |
| 12 | demo `ls` + unknown cmd | 0 | real listing + `unknown: try help` (screenshot) |

## 4. Every test run and any failure

Zero failures in this campaign (12/12 green).
Historical: Phase N runs 1–2 (T6 FAIL ×2, serial
interleave root-caused + fixed), runs 3–4 green.
Combined with this campaign: 5 consecutive full-
suite greens since the fix; 4 standalone panic
passes. No unexplained failure remains.

## 5. Fault-test fix review

- Scope: inside `#ifdef NOVA_FAULT_TEST` only;
  production path byte-identical (verified diff).
- Balance: cli → one serial_puts → sti, no early
  return inside; boot-thread IF=1 context; no
  lock/scheduler/syscall interaction (serial takes
  no locks).
- Not expanded beyond necessity (single print).
- T6 checks behavior (full marker set + trap +
  panic + trace), the marker is its sync point —
  legitimate, not string-theater.
- Serial now holds exactly one contiguous
  `INJECT-FAULT` (counted). Fix judged SAFE and
  SUFFICIENT. No correction made.

## 6. Shell exit behavior review

- Prints `bye; reset the VM for a new shell`
  BEFORE `nova_exit` (screenshot-proven on screen).
- Accurate: session ends, VM keeps running, reset
  is the recovery (demo procedure confirms).
- Implies no shutdown (says nothing about the
  kernel/VM stopping). Safe to repeat (each exit
  ends one session cleanly; no panic, no leak
  beyond accepted no-reap).
- No ABI change (same SYS_EXIT). Docs match
  (CLI.md + BETA-DEMO). The reverted init.c
  tombstone stays reverted (correctly unreachable).
- No correction made.

## 7. Final demo result

All BETA-DEMO.md steps executed for real:
build → image → boot banner + proofs + prompt
→ `stat` live → `cat /nope` clean error →
`exit` guidance → poweroff. Screenshots:
`n1_demo2/3/4.png` (session record, host temp).
`ls` listing and unknown-command path verified
on screen. Recovery procedure (VM reset) is the
documented next step, not yet needing demo
(user resets via VBox, standard).

## 8. Files changed (this phase: none)

No code, no docs, no config changed in N.1.
Usability allowance (§8) deliberately unused:
reviewed boot/discoverability/errors/diagnostics/
exit/repro/limits — no issue met the bar
(reproducible defect + low risk + ABI-neutral +
regression-testable). Word-wrap cosmetics and
history/completion stays deferred by design,
not oversight.

## 9. Known issues (carried, unchanged)

Phase N §C list stands (12 items). Plus: T6's
30 s budget has ~12 s margin (standalone ~17–18 s)
— environmental slowness could trip it again;
stands as TEST-HARNESS sensitivity with a
documented signature (full panic present, one
marker split/missing), never to be mistaken for
product failure without the marker analysis.

## 10. Readiness verdict

**PASS — VERIFIED BETA.** Fresh build, 3× suite,
gate, panic ×2, and screen-proven demo all pass;
the one historical failure is root-caused, fixed,
and green across 5 consecutive suites; no
unexplained failures in this campaign. No commit,
no push, no tag. Working tree left for review.
