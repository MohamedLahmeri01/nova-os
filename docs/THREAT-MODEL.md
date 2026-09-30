# NOVA OS — Threat Model (Phase 0)

Status: normative for design reviews. No mitigations claimed as done.

## Assets

Kernel memory/code, page tables, credentials, handles, filesystem
integrity, network availability/integrity, boot chain, build chain
(reproducibility/SBOM), developer credentials.

## Actors (adversaries)

1. Malicious application (unprivileged user code)
2. Malicious/buggy driver or service
3. Compromised userspace service
4. Malformed filesystem image / network packet / user input
5. Local privilege-escalation attacker (any bug → ring0)
6. DMA-capable device (future: IOMMU requirements noted, not done)
7. Supply-chain attacker (deps, toolchain, CI)

Out of scope for 0.x: physical attacks, microarchitectural side
channels beyond SMEP/SMAP/NX gating, encrypted-filesystem secrecy.

## Attack surfaces (Phase 1 first!)

- Boot-info parsing (MALFORMED memory map / module) → validate all
  ranges, overlaps, sizes before use.
- Serial/console output (info leak) → no kernel pointers/ASLR secrets
  in logs by default.
- Syscall boundary (every pointer/length/handle) → per-call
  validation matrix; fuzz from Phase 5.
- Page-table / allocator bugs → guard pages, no RWX, W^X for kernel,
  poison-on-free in debug builds.
- Driver/hardware input (PCI BARs, virtio rings) → bounds-check every
  device-provided index/length.

## Per-interface review checklist (mandatory)

```text
[ ] all inputs enumerated (pointers, lengths, handles, enums)
[ ] integer overflow considered on every size+offset
[ ] ownership/lifetime stated; close/error paths invalidating
[ ] least privilege: does this need ring0 / this capability?
[ ] logging: no secrets, actionable on failure
[ ] tests: positive + negative + fuzz seed listed
```

## Response process (staging, see SECURITY.md)

Private report → triage (severity per CVSS-ish scale) → fix in
`security/*` branch → regression test → advisory. No public disclosure
of exploitable detail before a fix is available, per master spec §37.
