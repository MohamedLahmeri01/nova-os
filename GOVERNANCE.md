# Governance (staging, Phase 0)

Status: proposed. No maintainers are formally seated yet.

## Roles (to be filled)

- Maintainers, subsystem maintainers, technical steering (TBD),
  release maintainers, security team (see SECURITY.md).

## Rules until seated

- Protected branches: `main` (stable), `develop` (integration).
  Feature/fix/security branches per master spec §58.
- Every merge needs: review + green gates (format, lint, compile,
  unit, and once Phase 1 lands, VBox boot test).
- ADRs accept by maintainer sign-off; license ADR (0008) needs
  explicit ratification before any release is called open source.
- No knowledge silos: every subsystem needs docs + a second reviewer
  able to cover.
