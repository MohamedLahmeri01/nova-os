# Security policy (staging, Phase 0)

No releases exist; this documents intent, not coverage.

## Reporting

- Do NOT open public issues for suspected vulnerabilities.
- Contact path: TBD (private mailbox to be published at first release).
- Include: affected version/commit, repro, impact, severity guess.

## Handling

Triage → fix in `security/*` branch → regression test → advisory.
Public disclosure of exploitable detail waits for a fix where
appropriate (see `docs/THREAT-MODEL.md`).

## Scope

Everything in this repo once it becomes executable. Build/CI secrets
and developer workstations are out of scope for this policy.
