# NOVA OS v0.8.0 — Pilot Issue Triage (Beta V1.4)

No issues reported. No testers have participated
yet. This file defines the process; entries land
here only from real, reviewed evidence.

## Severity scale

- P0 — data loss, unsafe destructive behavior,
  critical integrity/security issue. Withdraw
  the candidate if reproducible.
- P1 — reproducible boot failure, persistent-
  data corruption, or blocker preventing normal
  use. Fix in a separately identified candidate.
- P2 — significant feature failure or confusing
  setup instruction. Fix or document.
- P3 — cosmetic defects, minor usability notes,
  suggestions. Backlog.

## Entry format (one per defect)

```text
ID:        V14-001, V14-002, …
Reporter:  pseudonym or consented identifier
Candidate: zip SHA-256 (must match frozen value)
Host/VM:   OS, VBox version, CPU/RAM/storage
Steps:     exact reproduction
Expected:  per contract/quickstart
Actual:    observed behavior
Evidence:  screenshot/log excerpt (redacted)
Repro:     yes/no/inconclusive + fresh-VM check
Severity:  P0–P3 + confidence
Action:    fix / document / defer / withdraw
```

## Rules

- Feature requests are not defects unless they
  contradict documented behavior.
- A missing report is NOT RUN, never PASS.
- An unreproduced bug is INCONCLUSIVE until the
  environment and steps are investigated.
- Never relabel the frozen candidate; fixes
  produce a NEW candidate + NEW checksum.
- No personal data without consent.

## Pre-seeded watch items (NOT defects — verify or refute)

- W-01: external-host egress (contract says
  untested; first tester with internet may try).
- W-02: T6 30 s budget on slower hosts.
- W-03: 8.3 uppercase confusion (`V8.TXT`).
- W-04: widget-over-console scribble discovery.

## Log

(empty — no reports received)
