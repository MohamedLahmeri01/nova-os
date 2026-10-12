# NOVA OS — v0.8 Report: Prototype Capability Expansion (Release Candidate, NOT Released)

## Verdict

**PASS — V0.8 BETA PROTOTYPE READY FOR REVIEW.**
One real capability (reboot-persistent files),
proven in-guest + across genuine reboot, fully
regressed, honestly bounded. No commit, no push,
no tag, no release publication.

## Starting / ending HEAD

- `0b5da64` → `0b5da64` (`main`, working tree).
- v0.5 `59ea07b` ancestor confirmed, intact.

## Initial working-tree condition

Phase N/N.2 code + beta docs + prior audits +
screenshot, VM poweroff. No new user changes.

## Candidate features evaluated

- A persistent storage: SELECTED. Foundations
  already complete (ATA R/W sectors, FAT + NOVA-FS
  write paths with selftests, syscall graft
  dispatch for create/write/delete, shell
  put/cat/rm). Verified live BEFORE committing to
  it: put→cat→poweroff→cold boot→cat identical
  on both volumes.
- B arrows/history: REJECTED (E0 dropped in kbd;
  line editing needs driver+redraw rework; N.2
  `!!` covers repeat; risk/value wrong).
- C network validation: REJECTED as capability
  (loopback+gateway already proven; external
  egress is validation-only, no code missing).
- D task diagnostics: REJECTED (needs new syscall;
  brief requires demonstrated need + compat
  review; none established).
- E reliability-as-feature: folded in — the
  campaign fixed 1 real regression + 1 race
  (below) instead of inventing work.

## Primary capability selected and why

Reboot-persistent user files: highest user-visible
value (the beta's most tangible gap: everything
evaporated), lowest risk (paths already written
and self-tested; only the lifecycle battery +
demo proof were missing), fully VBox-testable,
no ABI/VFS/driver changes.

## Actual implementation changes

1. `kernel/syscall/syscall.c` (+~95):
   `fs_persist_selftest()` — create/write/grow/
   reread/delete/gone on live /disk + /nova,
   byte-exact over 1024B, NOVA fsck-clean after,
   `PERSIST-OK`. Placed here (not file.c) per the
   established guest-only pattern: host unit tests
   do not link cross-driver symbols (a first
   placement in file.c broke T9/T12 links; caught
   by the suite, fixed same session, file.c
   reverted byte-identical, hash-verified).
2. `kernel/fs/fs.h` (+6): decl only.
3. `kernel/pm_main.c` (+12): wire battery after
   fs_concurrency; atomic GATES-OK print (same
   interleave class as the T6 marker — observed
   `GATES-O`+banner in one boot log).
4. `VERSION`: `0.2.0` → `0.8.0` (single
   authoritative source; no tag created).
No scheduler/MM/lock/timer/ABI/VFS changes. No
new dependencies (third-party added: none).

## Tests added and executed

- New: PERSIST-OK battery (in-suite, every boot).
- Full suite 23/23 ×3 consecutive (fresh builds);
  one run caught the T9/T12 link regression
  (21/23 + CORPUS 6/7) — recorded, fixed, green
  after. `ci/gate.py` PASS; lint clean.
- Panic path: in-suite T6 ×3 green (no standalone
  runs: no timer/irq change).
- Manual: write/read/reboot/read/delete on both
  volumes + `ls /disk`, all screenshot-proven.

## Build and emulator outcomes

- build ok (162772B), image attached, cold boots
  ~16–25 s, all markers incl. PERSIST-OK +
  GATES-OK, poweroffs clean.

## Consecutive full-suite results

23/23, 23/23, 23/23 (post-fix). Plus V08.1
baseline 23/23 pre-change. Zero unexplained
failures.

## Demonstration outcome

V0.8-DEMO.md executed end-to-end on release bits:
put → cat → poweroff → cold boot → cat identical
(shots v8_demo1–3), rm cleanup, VERSION 0.8.0 on
screen (`NOVA OS 0.8.0 init`, `ver=0.8.0`).

## Version metadata changes

`VERSION` file only (`0.2.0`→`0.8.0`; verified
single source — no hardcoded duplicates in
kernel/userspace). Init banner + uname reflect
it (screenshot-proven). No tag (policy).

## Known defects and deferred work

- Durability limits (V0.8-SCOPE contract): no
  mid-write crash claims, 64 KB cap, fixture VDIs,
  no shared-VDI use. B/C/D candidates deferred
  with reasons above. External egress untested.
  12 pending arch decisions untouched.

## Complete changed-file list

- M `kernel/syscall/syscall.c`, `kernel/fs/fs.h`,
  `kernel/pm_main.c`, `VERSION`, `CHANGELOG.md`,
  `docs/CLI.md`, `docs/PRODUCTION-READINESS-MATRIX.md`.
- New: `docs/V0.8-SCOPE.md`, `V0.8-BASELINE.md`,
  `V0.8-DEMO.md`, `NOVA-V0.8-REPORT.md` (this file).
- Untouched: everything else, all prior reports/
  ADRs/manifest/screenshot. file.c reverted clean.

## Dependency and licensing changes

None. No imports, no vendoring, no license change.

## Baseline and user work intact

`59ea07b` history untouched; no reset/clean/stash;
screenshot + audits preserved; disposable VDIs
only; no host-disk contact.

## Reproducing the v0.8 build and demo

```text
python tools/nova.py build
python tools/nova.py image
(start NOVA-OS-CONSTRAINED; click to capture)
put /disk/v8.txt hello-v08
cat /disk/v8.txt
(poweroff; cold boot)
cat /disk/v8.txt
rm /disk/v8.txt
python tools/nova.py test     # 23/23
```

## Confirmation

No commit. No push. No tag. No release
publication. Full diff reviewed hunk-by-hunk.
