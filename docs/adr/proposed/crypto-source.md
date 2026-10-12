# Proposed ADR — Cryptographic source: one vetted library, narrow seam (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Zero crypto in-tree (grep-verified). Needed for
TLS, signatures, attestation, storage encryption.
Options: (a) adopt one minimal vetted library
behind a narrow NOVA seam (RNG → primitives →
certs), callers never touch internals; (b) per-
algorithm picks (reject: version skew, audit
sprawl); (c) hand-roll (reject absolutely: no
novel crypto without cryptanalysis, which is not
this team).

## Trade-offs

(a) concentrates trust and review on one
dependency + one seam. Costs: porting (freed-
standing, no-POSIX, constant-time preserved),
entropy integration (CSPRNG + failure policy is
part of this ADR, not separate), license check
(Apache-2.0-compatible distribution), and a
maintenance contract (who patches CVEs).

## Prerequisites / evidence to decide

Published candidate scorecard (license, size,
portability, upstream health); known-answer +
wycheproof-class vectors running in-guest;
entropy-source analysis per board; seam review
(callers cannot misuse: nonces, padding, cert
validation hostile-first).
