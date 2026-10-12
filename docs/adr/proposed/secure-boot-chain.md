# Proposed ADR — Secure boot chain (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Stage1 trusts BIOS; nothing is verified (matrix:
entire trust column ABSENT except RO text).
Options: (a) staged chain — stated root of trust
(fuses/ROM/TPM where present, locked flash +
provisioned keys where not) → verified loader →
verified kernel → verified services, each stage
checking the next, with measured-boot records
where hardware allows; (b) verified-everything-
at-once (reject: unreviewable, untestable);
(c) skip to OTA signing only (reject: verifies
the package, not the boot — different threat).

## Trade-offs

(a) is implementable per-board and honest about
weak roots (a locked-flash root is stated as
weaker than TPM, not equivalent). Never conflate:
measured ≠ secure, encrypted ≠ authenticated,
one signature ≠ a chain. Costs: provisioning
flow, key ceremony, revocation story, per-board
porting of the early chain.

## Prerequisites / evidence to decide

Crypto source + keys story (M3); first board
named (chain is per-board); hostile-boot tests
(tampered stage/kernel/service each refused);
rollback rules live here with OTA (shared doc).
