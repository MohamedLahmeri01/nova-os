# NOVA OS v0.8.0b — Pilot Results (Beta V1.5)

Candidate under test: `dist/NOVA-OS-Beta-v0.8.0b.zip`
SHA-256:
`c259c52f8ffacbb5b3089621c76613839e865f044beff5e53a373a6d9ab0ce87`
(verified this phase; inner checksums all match;
disk images bit-identical to frozen v0.8.0).
Frozen v0.8.0 zip re-verified unchanged
(`b6c24bc6…a01208`). No candidate was modified.

## Owner invitation (share manually, unedited except bracketed parts)

> You're invited to privately test NOVA OS v0.8.0b,
> an experimental x86-32 operating-system prototype
> for VirtualBox testing (text console, small shell,
> reboot-persistent files, gateway ping + public DNS
> — no TCP/web, no mouse). Enclosed: the zip, whose
> SHA-256 must match [see manifest] before you boot
> anything. Rules: disposable VM + supplied images
> only, never a real disk, no sensitive data in the
> guest, redact secrets from reports. Start at
> README.md, follow the quickstart, fill the
> feedback template, and send it back to [owner
> channel]. Expect rough edges; report exactly what
> you see, including confusion. Thank you.

## Cohort status

- Testers invited by owner: UNKNOWN (owner action;
  this phase recruits no one and sends nothing).
- Reports received: 0.

## Results matrix (all NOT RUN — no evidence yet)

Checksum / extract / VM setup / first boot /
shell / help-about-uptime / persistence create /
poweroff / cold boot verify / removal / setup
recovery / unaided-usability / gateway ping /
off-subnet refusal / public DNS: NOT RUN ×3
columns (A/B/C). A missing report is NOT RUN,
never PASS.

## Locally verified reference (same bits, developer-run)

Clean-VM boot, first run, persistence + removal,
gateway ping, off-subnet refusal, public DNS
(`example.com has 104.20.23.154`) — all PASS
with screenshots (prior campaigns). Listed here
so future tester results can be compared, not to
stand in for them.

## Outcome

**PILOT PREPARED — HUMAN VALIDATION PENDING.**
No independent person has completed a first run
in this phase. Do not claim usability validation.
Next action: owner invites 3 testers (5 max after
early results) and records outcomes here.
