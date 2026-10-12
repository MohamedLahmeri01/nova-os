# NOVA OS — External Beta Report (release preparation, NOT a release)

## Verdict

**PASS WITH LIMITATIONS — PRIVATE TESTING ONLY.**
The package is self-contained, checksum-verified,
legally distributable (Apache-2.0, no copyleft
payload, no vendored blobs), documented, and
proven in a clean VM end-to-end. Limiters keep it
private: single-host/single-VBox verification,
external egress untested, no crash-safety evidence,
no second-human archive test. Nothing here
authorizes publication — sharing is a manual
owner decision with the checklist below.

## What was built and proven

- 5 beta docs (contract, quickstart, release
  notes, known issues, feedback template) +
  package README — all verified line-by-line
  against code and live runs (including two
  factual corrections made during writing:
  no-ACPI-shutdown, E0-drop history note).
- Package: 14 files + structured zip, hashes
  recorded and independently recomputed.
- Clean VM: created→booted→first-run→persist→
  poweroff→reboot→verify→cleanup→deleted, all
  green with screenshots. Dev VM never touched.
- Regression: 23/23 ×3, gate, panic, lint —
  all on the exact bits shipped (hash-matched).

## Inherited vs fresh evidence

- Fresh this campaign: everything above (build,
  suite, package, clean-VM run, checksums).
- Inherited (not re-run): 135 s soak, T6-split
  forensics, wire/FS corpora internals — all on
  record in prior reports, unchanged code paths.
- Not reproduced by anyone yet: second human,
  second host, second VBox version.

## Open issues and risks

- Single-verifier risk (mitigation: checklist
  below forces independent confirmation).
- External egress untested (testers will try it;
  contract says so explicitly).
- Mid-write loss untested (BLOCKED rig).
- T6 30 s budget sensitivity (documented).
- Font provenance: RESOLVED this campaign
  (BIOS-loaded at runtime, no blob in tree).

## Next actions (owner)

1. Review this report + the package listing.
2. `sha256sum -c` (or certutil) on the zip.
3. Hand the zip + quickstart to tester #1.
4. Collect FEEDBACK-TEMPLATE responses.
5. Triage into: doc fix / harness / real defect.

## Changed files (this campaign)

- New docs: EXTERNAL-BETA-CONTRACT/QUICKSTART/
  RELEASE-NOTES, KNOWN-ISSUES, FEEDBACK-TEMPLATE,
  EXTERNAL-BETA-VERIFICATION, this report.
- New artifacts (gitignored dist/, on disk): package
  dir (14 files) + zip. Prior releases live there
  the same way; handoff is the zip FILE, not git.
- Modified: none (no source, no existing docs).
- VM registry: test VM created and fully removed;
  only pre-existing VMs remain.

## Confirmation

No commit, no push, no tag, no publication, no
upload, no visibility change. Tree left for review.
