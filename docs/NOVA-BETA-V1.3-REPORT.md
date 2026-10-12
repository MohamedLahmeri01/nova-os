# NOVA OS — Beta V1.3 Report: Independent Regression, Persistence Stress & Release Qualification

## 1. Verdict

**PASS — V1.3 BETA QUALIFIED FOR EXTENDED DEVELOPER TESTING.**
Fresh build, 23/23 ×3 post-fix, gate, panic ×2,
independent reboot-persistence reproduction
(multi-file, 2 cycles), fsck-clean throughout,
one real failure found-fixed-proven, limits
documented. Crash safety explicitly NOT claimed
(interruption testing BLOCKED).

## 2–3. HEAD and initial tree

- `0b5da64` → `0b5da64` (`main`); `59ea07b`
  ancestor true. Initial tree: v0.8 working
  changes + prior audits + screenshot, VM
  poweroff — all preserved, none disturbed.

## 4. Baseline results

lint clean; build 162772B; image attached;
suite 23/23 (run 1); gate PASS; panic PASS.
Skips 0; T6/T9/T12 all present and passing.

## 5. Persistence contract verified

Backing (IDE slave maps), init callbacks, exact
put/cat/ls/rm semantics incl. no-shrink,
8.3 uppercasing, 64 KB/256 B/32 B caps, errno
behavior, synchronous write-through (no write-
back cache), poweroff assumptions — all in
BETA-V1.3-PERSISTENCE-CONTRACT.md, each tied to
code lines or live evidence.

## 6–8. Lifecycles, cold boots, cycles

- Byte-exact: 2 B–10 B payloads with spaces/
  punctuation, cross-volume, post-reboot —
  PASS (P-01/02/05).
- Overwrite semantics: stale tail confirmed
  live (`vlsekyess!`) — contract behavior, PASS.
- rm/recreate/read-after-remove: PASS (P-04/05).
- Missing/nested-missing: clean errors, PASS.
- 2 full create/read/poweroff/boot/read/remove
  cycles, distinct contents: PASS (P-06).
- Empty-text put + 64 KB shell boundary:
  NOT RUN / NOT APPLICABLE with reasons (usage
  gate; 128 B line cap; driver covers 1 KB).

## 9. Boundary and integrity results

- Offsets/lengths capped pre-use; chunked
  copies; fail-closed partial counts; lifetimes
  verified live; locks/IRQ per established
  context — no defect found (audit, not proof).
- nova_fsck clean before/after; PERSIST-OK
  every boot. No corruption observed; no repair
  tested or claimed.

## 10. Crash/interruption: BLOCKED

No mid-write injection possible (synchronous
ms-scale writes; no harness hook; external
poweroff all-or-nothing). Clean-reboot evidence
is not crash evidence. Missing capability named:
timed power-cut rig.

## 11. T9/T12 verification

file.c hash == HEAD blob (byte-identical revert;
reference stated). T9/T12 present + passing in
all runs. Battery guest-only in syscall.c.
Final image post-dates last code change.

## 12. Suite and CI

23/23 (run 1), 22/23 T6 (run 2, F-01), 23/23 ×3
(runs 3,4,5 post-fix). Gate PASS. Panic ×2 PASS.
Lint clean. Zero unexplained results.

## 13. Defects found and fixes applied

- F-01 (this campaign): T6 INIT-OK split
  (`INIT-`+banner) — same interleave class as
  INJECT-FAULT/GATES-OK. Fix: atomic print in
  spawn_init (+7/-1, pm_main.c only). Proven by
  3 consecutive greens + contiguous marker.
- No other defects. No features added. No
  dependencies added.

## 14. Changed-file list (this campaign)

- M `kernel/pm_main.c` (+7/-1: INIT-OK guard).
- New: BETA-V1.3-PERSISTENCE-CONTRACT.md,
  BETA-V1.3-TEST-RESULTS.md,
  NOVA-BETA-V1.3-REPORT.md (this file).
- Disks: campaign files created AND removed
  (verified `ok` + post-remove ENOENT); volumes
  left with only selftest scratch.
- Untouched: everything else, all history,
  screenshot, manifest, VERSION (stays 0.8.0).

## 15. Limitations and unresolved risks

Durability contract limits (no crash claims,
64 KB, fixtures); interruption BLOCKED;
external egress untested; soak stays prior-
phase; 12 arch decisions untouched; T6 30 s
budget margin (~12 s) noted as harness
sensitivity with a documented failure signature.

## 16. Reproduction commands

```text
python tools/nova.py lint
python tools/nova.py build
python tools/nova.py image
python tools/nova.py test        # 23/23
python ci/gate.py
python tools/nova.py panic-test
(boot; put/cat/rm per V0.8-DEMO.md; poweroff;
 cold boot; cat again)
```

## 17. Confirmation

No commit, no push, no tag, no release
publication. Full diff reviewed. Tree left
for review.
