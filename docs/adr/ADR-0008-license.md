# ADR-0008: License evaluation (NOT RATIFIED)

Status: proposed (Phase 0). Date: 2026-09-29. Decision: DEFERRED.

## Context

Must weigh commercial adoption, driver compat, ecosystem compat,
contributor licensing, patents, redistribution, proprietary extensions.

## Evaluation

- MIT/Apache-2.0: maximal adoption, simple contribution, weak
  patent/explicit-grant story for MIT (Apache-2.0 fixes that).
- GPLv2/v3, MPL-2.0: stronger copyleft but driver/ecosystem friction.
- Recommendation (provisional): **Apache-2.0** for code (patent grant,
  permissive, ecosystem-friendly), with a later decision on docs/fonts
  (CC-BY-4.0 candidate). This is NOT ratified.

## Consequences

- No `LICENSE` grant is effective until this ADR is accepted and the
  full license text + headers land. Until then all files are
  all-rights-reserved by default; do not redistribute as open source.
- Acceptance requires explicit maintainer sign-off (see GOVERNANCE.md).
