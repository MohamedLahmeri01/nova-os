# NOVA OS v0.8.0b — Pilot Issue Triage (Beta V1.5)

Candidate: `dist/NOVA-OS-Beta-v0.8.0b.zip`
(`c259c52f…a01208`). Frozen v0.8.0 preserved
(`b6c24bc6…a01208`, re-verified).

## Severity scale

- P0 — data-integrity/safety-critical failure.
  Reproducible P0 withdraws the candidate.
- P1 — reproducible boot failure, persistent-
  data corruption, critical documented-function
  failure. Fix → NEW candidate + NEW checksum.
- P2 — significant usability issue or
  noncritical functionality defect.
- P3 — cosmetic or suggestion.

## Entry format

ID (V15-001…) / reporter pseudonym / candidate
hash / host + VM config / tests attempted /
expected vs actual / steps / evidence /
fresh-VM reproduce / severity + confidence /
next action. No personal data without consent.
Unimplemented-but-documented capabilities are
not defects. Missing reports are NOT RUN.

## Watch items (NOT defects — verify or refute)

- W-01: external-host egress attempts.
- W-02: T6 30 s budget on slower hosts.
- W-03: 8.3 uppercase confusion.
- W-04: widget-over-console scribble discovery.
- W-05: quickstart step failures on non-Windows
  hosts (paths/commands differ).
- W-06: `put` empty-text refusal confusion.

## Log

(empty — no reports received; pilot prepared,
human validation pending)
