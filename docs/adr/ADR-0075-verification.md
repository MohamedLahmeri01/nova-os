# ADR-0075: Verification hardening — mapping audit, warnings, CI semantics (v0.4 D/Q/R)

Status: accepted (v0.4). Date: 2026-10-06.

## Context

Three v0.4 verification gaps shared one shape: correct code
with no runtime proof (mapping permissions), tolerated
noise with no policy (warnings), and a gate that couldn't
tell "no VM" from "pass" (CI). Each is closed below; the
security-model document (`docs/SECURITY-MODEL.md`) is
ratified here as the normative companion to the Phase-0
checklist.

## Decision

- **Runtime permission grid** (`vmm_audit_mappings`,
  `MEMMAP-OK`): null RO-supervisor, text RO-supervisor
  (extends the -7 asserts with the U/S bit), data/VRAM
  RW-supervisor, large pages RW-supervisor, gap unmapped,
  plus a scratch addrspace proving clone-isolation and
  exact user-map bits (URW + URO). Split across
  `vmm_selftest` (static half) and `vmm_audit_user`
  (heap half — the first version hung the boot by
  calling kmalloc before `kheap_init`; the split is the
  fix, documented so the ordering rule survives).
  `KERNEL_PDS` promoted to the shared header (was
  duplicated).
- **Security model ratified**: `docs/SECURITY-MODEL.md`
  (trust/components/inputs/boundaries with per-boundary
  trust/validation/lifetime/permissions/failure/
  concurrency/ownership/weaknesses + live weakness
  register W1–W6, including the corrected W2: user
  cannot READ kernel maps, the gap is data execution +
  no randomization). THREAT-MODEL.md stays the
  per-review checklist; the model is the architecture
  record.
- **Warning policy** (`docs/WARNINGS.md`): one tolerated
  guest warning (jail defensive frame), three fixed
  zero-risk (virtio sign, irq cast, smp dupes), host-only
  classes documented (SYS_OPEN clash kept loud
  deliberately; 64-bit cast noise; font heuristic).
  `-Werror` waits on an empty file + a host strategy —
  decided, not drifted.
- **CI gate semantics** (`ci/gate.py`): env versions
  recorded, lint+build always, image gated on real VBox
  presence (well-known path + PATH), final line PASS vs
  PARTIAL(vm-skipped) — SKIP is reported, never a pass.
  No workflow YAML ships without a runner to execute it.

## Evidence

`MEMMAP-OK` in guest; lint clean (1 tolerated); gate
PASS on the reference host; full suite at commit.

## Hard lessons

- Heap-before-init hangs look like silent stops (no
  panic, no log tail): when a boot halts after paging
  with no error, suspect allocation before `kheap_init`
  first. Added to the debugging pattern catalog by this
  note.
- A duplicated constant (`KERNEL_PDS`) across two files
  survived because both values agreed: promoted to one
  home before they could disagree.

## Consequences

- Mapping permissions are now runtime-proven, not
  source-argued; any future mapping change trips
  MEMMAP-FAIL with the exact class named.
- Warning count is a release-blocking number with an
  owner (this ADR): 1 guest, host classes documented.
