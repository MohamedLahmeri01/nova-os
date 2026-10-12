# NOVA OS — Phase N Report: Beta Prototype

## Verdict

**BETA PROTOTYPE — LIMITED TESTING** (useful subset
proven; blockers named; no production claims).
Two implementation tracks, both green; demo
executed with screenshot evidence; full diff
reviewed below; no commit, no push.

## Starting / ending HEAD

- Start: `0b5da64` (branch `main`).
- End: `0b5da64` (uncommitted working tree).
- Baseline `59ea07b` ancestor: confirmed, intact.

## Initial working-tree condition

`M docs/THREAT-MODEL.md` + untracked prior-audit
docs + `docs/adr/proposed/` (20 files) + one user
screenshot (preserved). VM poweroff (safe to boot).

## Feature tracks selected and why (max 3, chose 2)

1. Deterministic fault-test gate (boot/
   diagnostics): T6 failed twice consecutively —
   a gate, not a nit. Root cause beats reruns.
2. Honest session end (usability): `exit` was a
   silent dead end; beta users hit it in minute
   one. Respawn rejected (would leak unreaped
   processes); guidance chosen (prints, honest).
No third track: nothing else earned code (console
proven, all 34 commands real, network sufficient).
TCP/eval deliberately not started (demo doesn't
need it; boundary preconditions unmet).

## Implementation completed

1. `kernel/pm_main.c`: cli/sti around the
   INJECT-FAULT print (fault-test `#ifdef` only;
   production binary path untouched).
2. `userspace/init/sh.c`: `exit` prints
   `bye; reset the VM for a new shell` before
   `nova_exit`. (An init.c tombstone was tried
   and REVERTED after proving unreachable: the
   exiting thread never returns to ustart.)
No ABI, scheduler, MM, lock, or timer changes.
No new dependencies (third-party added: none).

## Tests executed and actual outcomes

- Full suite ×4: 22/23 FAIL, 22/23 FAIL,
  23/23 (fix), 23/23 (confirm).
- Standalone panic-test ×2: PASS (~17–18 s).
- `ci/gate.py`: PASS. `lint`: clean. `run-vbox`:
  PASS (~16–19 s) on final image.
- Manual screenshot tests: console boot, `stat`
  (live values), `cat /nope` (clean error),
  `exit` (guidance, no panic). All PASS.
- T6 FAIL root cause: serial interleave
  (`INJECT`+init banner+`-FAULT`) from the
  concurrently-running init thread; product
  code correct every run (panic flawless).

## Boot demonstration outcome

Executed end-to-end (BETA-DEMO.md): build →
image → headless boot → banner + proofs +
prompt on DISPLAY (screenshot) → `stat` →
`cat /nope` → `exit` guidance → poweroff.
All expected outputs observed.

## Known defects and limitations

Per BETA-SCOPE.md §C (12 items): x86-32/VBox-
only, BSP-only, no crypto/TCP/OTA/mouse, volatile
ramfs, no Linux ABI, no history/completion,
widget scribble, 40-col wrap, exit-needs-reset,
untested external ping/DNS, prior-phase soak
standing in for a new one.

## Files created and modified

- New: BETA-BASELINE/SCOPE/TEST-PLAN/DEMO,
  NOVA-PHASE-N-REPORT (this file).
- Modified: `kernel/pm_main.c`, `userspace/init/
  sh.c`, `docs/CLI.md`, `CHANGELOG.md`.
- Untouched: everything else, all prior audit
  docs, all ADRs, manifest, user screenshot.
- Diff: +2 code sites (7 lines), docs as listed.
  Reviewed in full (read every hunk).

## Baseline and user changes preserved

- `59ea07b` history untouched; v0.5 source
  recoverable (`git show 59ea07b:`).
- No reset/clean/stash; user screenshot intact;
  prior-audit docs unmodified.
- Destructive tests: none exist; none run; VM
  disks are harness-managed disposables.

## Build / boot / test commands (exact)

```text
python tools/nova.py build
python tools/nova.py image
python tools/nova.py run-vbox
python tools/nova.py test
python ci/gate.py
python tools/nova.py panic-test
```

## Remaining blockers

- External-network egress validation (needs NAT
  tests, not code).
- Longer soak (nice-to-have; prior evidence stands).
- The 12 ratified-pending decisions (unchanged;
  this phase needed none of them).
- Commit/push/tag: explicitly NOT done (await
  instruction).

## Ready for developer testing?

Yes — within the documented envelope and limits:
boots, consoles, commands, errors, and demo all
proven; nothing working is hidden and nothing
missing is advertised. **BETA PROTOTYPE —
LIMITED TESTING.**

## Confirmation

No commit. No push. No tag. No baseline change.
