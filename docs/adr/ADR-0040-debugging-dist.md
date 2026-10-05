# ADR-0040: Debugging guide + distribution packaging (11b)

Status: accepted (Phase 11b). Date: 2026-10-05.

## Context

Eleven phases of hard-won debugging recipes lived scattered
across ADRs, and releases were whatever happened to be in
`build/`. Phase 11b closes the SDK with operator-facing
material: a debugging guide and a verifiable package.

## Decision

- `docs/DEBUGGING.md`: the recipe ladder (serial -> panic +
  objdump -> markers -> pcap -> VBox.log -> host tests) plus a
  pattern catalog (symptom -> first question -> ADR) covering
  every major incident so far (frozen rings, shifted frames,
  hex/dec magic, direction bits, NULL restores, gate placement,
  stale docs/binaries).
- `python tools/nova.py dist` assembles
  `dist/nova-os-<date>-<sha>/` (binaries, ABI.md, changelog, SDK
  readme, manifest with date/commit/ABI version + per-file
  bytes+sha256). Never rebuilds (missing outputs fail loudly).
  `dist/` is gitignored like `images/` (produced, not stored).
- T18 assembles into a temp tree and re-verifies every manifest
  row (size + hash) plus the file set and header: the package
  proves itself each suite run.

## Evidence

`nova test` 18/18 (T18 distribution verifies).

## Hard lessons

- Manifest parsers must key off the table header, not field
  shapes: title lines (`NOVA OS distribution`) and metadata
  (`date: ...`) both split into three tokens. T18 caught it
  twice before the parser keyed on `file bytes sha256`.
- Release assembly without rebuilds keeps provenance honest:
  `dist` packages exactly what `build` produced (mtime chain
  intact), never a silent fresh compile.

## Consequences

- Phase 11 complete. Next: Phase 12 Desktop (explicitly last).
