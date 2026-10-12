# NOVA OS v0.8.0 — Private Pilot Plan (Beta V1.4)

## Purpose and scope

Collect independently reproducible evidence from
real testers that the v0.8.0 candidate is
understandable and runnable outside the
developer's environment. No feature work, no
candidate changes during the pilot.

## Candidate identity (frozen)

- File: `dist/NOVA-OS-Beta-v0.8.0.zip`
- SHA-256:
  `b6c24bc6b566ffc8fa3ff5cd81c9e40f46d4609a219f36f82bdafaa6a01208cb`
  (verified this phase; do not rebuild or
  overwrite it — build scripts cannot
  regenerate this fixed name).
- Source: HEAD `0b5da64` + working tree (v0.8
  code); v0.5 `59ea07b` ancestor true.
- Contents: 14 files (README, contract,
  quickstart, release notes, known issues,
  feedback template, LICENSE, VERSION,
  MANIFEST.txt, SHA256SUMS.txt, 4×1 MB .raw).

## Cohort

3–5 testers, private invitation by the project
owner. No accounts created, no messages sent,
no public exposure by this phase. Testers use
their own machines and disposable VMs only.

## Required test cases (per tester)

Checksum → extract → fresh VM → first boot →
shell (`help/about/uptime/stat`) → put/cat →
clean poweroff → cold boot → byte-exact verify
→ rm → unknown-command + bad-path errors →
`exit` guidance. Full matrix in RESULTS doc.

## Evidence required

Per-test PASS/FAIL/BLOCKED/NOT RUN/
INCONCLUSIVE; host OS + VBox version + VM
config; artifact hash; expected vs actual;
exact steps; serial excerpt or screenshot for
failures; fresh-VM reproduce check. Redacted
for secrets/personal data.

## Known limitations (tester-visible)

Contract + known-issues docs govern: no mouse,
no TCP/crypto/OTA/SMP, volatile ramfs outside
/disk+/nova, no-shrink overwrites, 8.3 upper,
64 KB cap, selftest scratch files, untested
external egress and mid-write loss.

## Data safety

Disposable VM + supplied images only. Never a
real disk, never shared images, never host data.
Back up anything valued from outside the VM.

## Completion criteria

Pilot complete when 3+ testers return full
matrices with no unresolved P0/P1, or when a
reproducible P0/P1 withdraws the candidate
(BLOCKED verdict). Missing reports are NOT RUN,
never PASS.
