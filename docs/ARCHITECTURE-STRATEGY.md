# NOVA OS — Architecture Strategy (post-v0.5)

Where the system goes from the validated v0.5 baseline
(HEAD `0b5da64`; v0.5 gates in RELEASE-MANIFEST.md),
and what it deliberately does not become. Companion to
ROADMAP.md (the plan), the readiness matrix (the facts),
and THREAT-MODEL.md (the adversary). The Phase-0
ARCHITECTURE.md remains the original design record;
where reality diverged, this file says so explicitly.

## 1. Product direction (summary; argued in ROADMAP §4)

Primary: secure, remotely managed edge/IoT
infrastructure OS (headless appliance profile).
Secondary: deterministic edge workloads with measured
timing. Non-goals: desktop parity, general server,
mobile. The testable competitive objective: a
deterministic, attested, remotely lifecycle-managed
edge node with proof-carrying releases — not "better
Linux everywhere."

## 2. Kernel architecture: monolith, kept — with seams

v0.5 is a monolithic kernel, and Phase-0's "hybrid/
modular" intent did not happen: filesystems above VFS,
the network stack, and drivers all live in ring 0
(gap W3 documents this honestly). Strategy: KEEP the
monolith through the first product, and earn isolation
in this order — (1) hardware memory protections (NX,
W^X, SMEP/SMAP where present), (2) a driver lifecycle
+ DMA policy that makes later isolation possible,
(3) userspace services only for new, crash-safe code
(network control plane, update agent), (4) driver
isolation last, when a second product needs it.
Rationale: the v0.5 proof corpus (gates, stress,
fuzz) is an asset bound to the current structure;
a microkernel rewrite discards it and re-opens
every audit. Revisit only with comparative evidence
(proposed ADR: kernel-architecture).

## 3. Platform architecture: abstract now, port once

v0.5 hardware knowledge is scattered (PIC/PIT ports
in irq.c/pit.c, 8042 in kbd.c, BAR handling in
virtio_net.c). Before any port: introduce
`arch_interrupt` + `arch_timer` + `arch_mmio`
abstractions behind the existing `arch_*` naming,
with the x86/PIC/PIT side as the first backend and
a host-stub second backend (today's NOVA_TEST_STUB
pattern generalized). Platform (board/firmware)
code separates from arch code: `boot/` normalizes
into the versioned `NOVOSBOOT` struct, which
already decouples firmware from kernel and must
gain UEFI + device-tree producers later without
changing its consumer contract. No ARM64 code
until the abstractions carry x86 with zero
behavioral delta (same gates, same serial bytes).

## 4. Portability strategy

- C99-ish freestanding kernel, fixed-width types on
  every ABI boundary (already true; keep by lint).
- 32/64 transparency from the ABI-STRATEGY (u32/u64,
  explicit padding) becomes enforceable the day a
  64-bit backend exists; until then it is a review
  rule, not a tested property — say so in reviews.
- x86-32 guest stays supported as the reference
  validation platform (cheap, deterministic, VBox),
  but is NOT the product future: product hardware is
  64-bit (x86-64 edge boxes first, ARM64 second).
  Proposed ADR: x86-32 support policy (validation-
  only after M5, with a named end-of-support).
- Endianness: little-endian assumed everywhere
  (parsers, images); record it as a porting note,
  not a bug.

## 5. Security architecture (implements THREAT-MODEL)

Chain, in dependency order: verified boot (from a
stated root of trust) → kernel memory protections
(RO text HAVE; NX/W^X/KPTI/SMAP next) → privilege
separation + caps/jails (HAVE, extend to sandbox
policy) → driver/DMA boundaries (policy first,
isolation later) → authenticated + encrypted
communications (needs crypto + CSPRNG first) →
data protection (FS crypto after crash-consistency
exists) → signed software + OTA with rollback →
identity + attestation (hardware-gated) → disclosure
+ patch lifecycle (create the process in M1; the
Phase-0 reference to SECURITY.md points nowhere).
Never conflate: measured ≠ secure boot, encrypted ≠
authenticated, signature-check ≠ chain of trust.

## 6. Networking strategy

Keep the layering that v0.5 proved (parsers →
demux → sockets, loopback-first, corpus-gated) and
extend strictly downward-then-up: (1) driver
diversity behind a NIC interface (virtio first,
one real NIC second — never two half-drivers);
(2) IPv4 completion (ARP hardening, ICMP discipline)
then IPv6 as a parallel stack, not an afterthought;
(3) TCP from an audited existing stack, never
hand-rolled (license + portability + maintenance
review first; proposed ADR: tcp-source); (4) socket
resource isolation per-process (quotas before
multitenant workloads); (5) TLS via a vetted
library against the new CSPRNG, with certificate
validation tested hostile-first (proposed ADR:
crypto-source covers both). Firewall/filtering is
policy + tests, not a milestone of its own.

## 7. Storage strategy

RAMFS stays the test/seed filesystem (fast,
deterministic, already proven). Production needs
one crash-consistent read/write filesystem, chosen
by comparative evidence (options: extend NOVA-FS
with journaling, adopt an existing embedded FS,
or port a proven subset; proposed ADR: fs-choice
— criteria are crash-consistency proof, flash
behavior, code size, license). Above it: a real
block-device abstraction (ATA becomes backend #1
of N), AHCI then NVMe, DMA only under the M2 DMA
policy. Embedded flash gets its own strategy
(wear-leveling/FTL assumptions stated before any
NAND driver). Encryption-at-rest waits for both
crash-consistency and a crypto home — order
matters, see the dependency graph.

## 8. Application compatibility strategy

Honest tiers, in order: (1) native NOVA SDK apps
(HAVE: 9-function libc, 3 sample binaries) for
first-party agents (update, telemetry, watchdog);
(2) a deliberate POSIX-subset profile (named calls,
each with validation + tests — NOT "POSIX" as a
blanket claim); (3) a Linux-ABI compatibility
assessment only after the syscall surface
stabilizes — compatibility without a frozen ABI
is a treadmill. Containers come after namespaces-
grade isolation exists; until then, "workload
isolation" means processes + caps + jails + quotas
(proposed ADR: posix-scope; proposed ADR:
isolation-model).

## 9. Operational model

The appliance is headless, attested, and boring:
verified boot → supervisor starts declared services
in dependency order → telemetry + audit stream out
→ signed updates arrive over mutually
authenticated channels → A/B apply with power-loss-
safe rollback → failures produce crash bundles, not
mysteries. Everything an operator touches is in the
SDK/docs: no hidden shell magic, no physical-
console privilege beyond the documented serial
policy. Fleet tooling (enrollment, grouping,
staged rollout) is product software built ON the
OS, tested against the same gates.

## 10. Principles (binding on all milestones)

1. Evidence over assertion: every new capability
   lands with tests + adversarial cases + docs, or
   it does not land.
2. Contracts before code: threat model, ABI policy,
   lock order, failure model — written first.
3. Narrow interfaces: small syscall surface, handles
   not pointers, bounded everything.
4. Keep the green green: the v0.5 gate corpus never
   regresses; new platforms earn their own gates.
5. Boring technology for trust: vetted crypto,
   proven stacks, standard formats — novelty only
   where NOVA differentiates (determinism proofs).
6. Say what is missing: limitations are documented
   next to the feature, never discovered by users.

## 11. Rejected alternatives (with reasons)

- Microkernel rewrite now: discards the proven
  corpus; isolation can be earned incrementally.
- Hand-rolled TCP/crypto/FS: attack surface without
  the review depth of vetted code; adopt + isolate.
- Desktop/mobile chase: 10,000-engineer treadmill,
  zero leverage from NOVA's strengths.
- "Secure" by checklist: mitigations without tests
  are claims; the matrix marks them ABSENT until
  proven.
- 32-bit product future: reference platform yes,
  deployment target no (memory, security features,
  ecosystem all assume 64-bit).

## 12. Amendment (Darwin-inspired architecture review)

Reconciliation with DARWIN-INSPIRED-ARCHITECTURE-
REVIEW.md. (1) §2's monolith stance refined:
modular-monolithic EVOLUTION (boundaries with
tests, one privilege domain) — the review's §4
comparison is the authority, still unratified.
(2) Portability (§3 here) gains the five-layer
model + per-boundary contract fields + the M1
portability test (review §5). (3) Driver thinking
(§2 order list) replaced by stages 1-2-3 with
IOKit-derived requirements (registry, matching,
families, work-loop discipline, power) re-
implemented in C, no kernel C++ (review §7).
(4) Compatibility (§8) gains the POSIX-vs-Linux-
ABI distinction and stages 1-5 (review §10).
(5) Trust chain gains its diagram (review §8).
History preserved: Phase-0 divergences noted in
§2 stand; this file remains strategy, not proof.
