# NOVA OS v0.8.0 — External Beta Verification (executed)

## Source and environment

- HEAD `0b5da64` (`main`); v0.5 `59ea07b`
  ancestor true; working tree uncommitted (docs +
  v0.8 code, full list in the final report).
- Host: Windows 11, Python 3.11.2, gcc 15.2.0,
  VirtualBox 7.2.6. No code changed during
  verification except where noted (none).

## Baseline gates (all PASS, exit 0)

- lint clean (1 tolerated warning). build ok
  (kernel 162772B). image attached.
- Full suite 23/23 ×3 consecutive, skips 0.
- `ci/gate.py` PASS. panic-test standalone PASS
  (in-campaign; T6 green in all 3 suites).

## Package under test

- Dir: `dist/NOVA-OS-Beta-v0.8.0/` (14 files:
  README, contract, quickstart, release notes,
  known issues, feedback template, LICENSE,
  VERSION, MANIFEST.txt, SHA256SUMS.txt, 4×1 MB
  .raw images regenerated pristine via `image`).
- Archive: `dist/NOVA-OS-Beta-v0.8.0.zip`,
  SHA-256
  `b6c24bc6b566ffc8fa3ff5cd81c9e40f46d4609a219f36f82bdafaa6a01208cb`,
  structure verified (top folder + images/).
- nova-os.raw SHA-256 matches the tested build
  (`527dad…`, same bits as the qualified image).
- Checksums independently recomputed: ALL MATCH.
- Secret/path scan: clean (only the words
  "credentials/tokens" inside redaction
  instructions). No host paths in tester docs.

## Clean-VM verification (isolated VM
`NOVA-OS-BETA-TEST`, created + deleted)

- Created per quickstart §3 (BIOS/PIIX3, 2 CPU,
  1024 MB, NAT virtio, images converted from
  the packaged .raw). Dev VM untouched.
- Boot: banner, INIT-OK, GATES-OK, 0.8.0 init,
  prompt + cursor on screen (shot bv_boot).
- First-run: `about` → version/target/abi=1;
  `put`/`cat` round trip; bad path → clean
  ENOENT text (shots bv_write, bv_short).
- Persistence: `put /disk/q1 q` → poweroff
  (VMState verified) → cold boot → `cat` → `q`
  → `rm` → `ok` → `cat` → ENOENT (shot
  bv_reboot). Packaged image proven, not just
  the dev image.
- Cleanup: VM powered off, unregistered,
  deleted; temp VDIs removed; only the two
  original VMs remain registered.

## Regression after packaging

- No source changed during verification; suite
  ×3 + gate already cover the packaged bits
  (nova-os.raw hash-identical to tested image).
- T9/T12 present and passing in all runs.

## Re-qualification (repeat campaign, same brief)

- Package re-verified: all inner SHA-256 match;
  zip hash identical (`b6c24bc6…`). Nothing
  changed since qualification.
- Fresh gates: lint clean, suite 23/23 ×3
  consecutive (skips 0), `ci/gate.py` PASS,
  panic-test PASS. Zero failures.
- Clean-VM evidence inherited (artifact bytes
  identical, hash-proven); no new clean-VM run
  needed or performed.
- Verdict re-confirmed: PASS WITH LIMITATIONS —
  PRIVATE TESTING ONLY (single-host verification
  remains the limiter).

## Limitations of this verification

- One clean VM, one host, one VBox version.
- External network egress not tested (NAT
  gateway only, as before).
- Mid-write power loss not tested (BLOCKED,
  no rig). Archive tested for structure +
  hash, not by a second human.
