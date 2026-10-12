# NOVA OS — Beta Baseline (Phase N0, executed 2026-10-09)

## Baseline commit and HEAD

- Validated v0.5: `59ea07b` (ancestor: confirmed).
- HEAD at phase start: `0b5da64` (branch `main`).
- Working tree at start: `M docs/THREAT-MODEL.md` (prior
  audit) + untracked prior-audit docs + `docs/adr/`
  `proposed/` (20 files) + one pre-existing user
  screenshot (preserved, untouched).
- VM `NOVA-OS-CONSTRAINED`: poweroff at start (safe to
  boot; 2 CPU / 1024 MB, IDE disk, NAT virtio-net).

## Tests run during this phase

- Toolchain (this host): Python 3.11.2, gcc 15.2.0
  (matches manifest's pinned toolchain).
- `python tools/nova.py lint` → clean (ADR 0001..0080,
  1 tolerated warning, pre-existing in syscall.c).
- `python tools/nova.py test` → 4 full runs:
  1. 22/23, T6 FAIL (see defect below).
  2. 22/23, T6 FAIL again (flake theory dead).
  3. 23/23 after the T6 determinism fix.
  4. 23/23 again (flake confirmed dead).
- `python ci/gate.py` → PASS (env + lint + build +
  image) after the fix.
- `python tools/nova.py run-vbox` → PASS (~16–19 s)
  on the final image.
- `python tools/nova.py panic-test` standalone →
  PASS (~17–18 s, exit 0) throughout.

## Previously reported results (not re-assumed)

- v0.5 Phase L: 23/23 + CORPUS-OK 7/7 + all OK
  markers + 135 s soak. Re-verified above except
  the soak (not repeated; no code path it covered
  changed except the fault-marker print).

## Tests not run

- Destructive host-disk tests (none exist; none run).
- Multi-VM or second-machine runs (no second VM).
- New tests added this phase: none as files — T6
  itself is the regression coverage (it caught the
  race); the demo commands are manual tests with
  screenshot evidence (see BETA-DEMO.md).

## Known baseline defects (pre-existing, kept)

1. T6 flakiness → ROOT-CAUSED this phase (serial
   interleave `INJECT`+banner+`-FAULT`; fixed by
   atomic marker print; 3 consecutive greens).
   Not pre-existing breakage: product code was
   always correct (panic flawless every run).
2. `exit` ends the session with no respawn (no
   process reaping — accepted v0.5 limitation).
   Mitigated: guidance now prints before exit.
3. Widget commands paint over the console (known;
   documented; `clear` recovers).
4. No mouse (MOUSE-ABSENT by design, documented).

## Reproduction commands

```text
python tools/nova.py lint
python tools/nova.py test        # 23/23, ~10 min, VM boots
python ci/gate.py
python tools/nova.py run-vbox    # cold boot PASS
python tools/nova.py panic-test  # fault path PASS
```

## Working-tree changes made this phase

- `kernel/pm_main.c`: atomic INJECT-FAULT print
  (fault-test-only block).
- `userspace/init/sh.c`: exit guidance line.
- Docs (new): BETA-BASELINE/BETA-SCOPE/
  BETA-TEST-PLAN/BETA-DEMO/NOVA-PHASE-N-REPORT.
- Docs (touched): CLI.md (exit note), CHANGELOG.md
  (beta entries). Nothing else. No commit, no push.
