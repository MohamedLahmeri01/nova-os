# NOVA Post-v0.5 Architecture, Security & Production Roadmap Audit

## 1. Executive conclusion

v0.5 is what it claims: a well-proven 32-bit x86
prototype confined to one virtual machine (23/23,
7/7 corpora, 50 in-guest markers, audited IF
discipline). Production (edge/IoT infrastructure)
needs ~55 further capabilities; zero are present
beyond prototype depth, and the audit names each
one with its dependency and milestone. Recommended
strategy: PRIMARY secure edge/IoT infrastructure
OS, SECONDARY deterministic profile inside it,
first product shaped as a headless appliance; keep
the monolith; adopt (never hand-roll) TCP, crypto,
and the journaled FS; ARM64 after x86-64 product
work, both after M1 abstractions. Five decisions
(§12) need human ratification before any related
implementation. No implementation code was changed
in this audit; no commit; no push.

## 2. Verified starting commit and tree status

DISCREPANCY (recorded, not blocking): the brief
expects HEAD `59ea07b` (v0.5 Phase L). Actual HEAD
at audit start: `0b5da64` — exactly two commits
ahead, both user-authorized after Phase L:
`143576c feat(cli)` (fbcon console) and `0b5da64
docs(cli)` (CLI manual). `59ea07b` verified as a
direct ancestor (`merge-base --is-ancestor` true).
Working tree: clean except one pre-existing
untracked file (the user's own screenshot,
`Capture d'écran …png`, untouched). `git diff
--check`: clean. Rationale for proceeding: the
delta is known, authorized, and additive; all
v0.5 properties verified at `59ea07b` still hold
at HEAD (fbcon/CLI-docs change display + docs
only); this audit is documentation-only and
cannot disturb the baseline. Scope statement: all
findings below are against HEAD `0b5da64`.

## 3. Methodology and commands run

- `git rev-parse HEAD`, `git log -1 --oneline`,
  `git status --short`, `git diff --check`,
  ancestor check above.
- `python tools/nova.py lint` → clean (ADR
  0001..0080 continuity, 1 tolerated warning).
- Full VM suite NOT re-run: last green (23/23 +
  gates) recorded at `0b5da64`; re-running boots
  the user's live VM and mutates dev artifacts —
  refused as unsafe per brief §14.8. Stated, not
  hidden.
- Targeted code search (grep: TCP/TLS/crypto/RNG/
  ARM/RISC-V → zero hits; USB/I2C/SPI/GPIO/WiFi/
  NVMe/AHCI/ACPI/UEFI → no drivers; mmap/fork/
  pipe/shm/signal → no impl; strcpy/sprintf →
  zero; TODO/FIXME → zero).
- Three read-only recon surveys (tree layout,
  ABI/libc surface: 9 libc functions, 38 syscalls;
  HW-touch inventory: PIC/PIT/8042/COM1/ATA/
  virtio/BARs; ATA PIO LBA28 + BMIDE DMA; LAPIC
  dormant) plus full reads of the v0.5 contract
  docs (DIAGNOSTICS, SECURITY-MODEL, LOCKING,
  THREAT-MODEL Phase-0, ARCHITECTURE Phase-0,
  ABI-STRATEGY, PHASE-L report, manifest).

## 4. Existing capabilities with evidence

Kept-verified v0.5 asset (~35 rows VERIFIED in the
matrix): BIOS stage1 + versioned boot-info struct;
PMM (bitmap + buddy) + VMM U/S split (MEMMAP grid);
RO kernel text; centralized user-copy validation
(38 sites, T8 vectors); strict-prio/RR scheduler +
starvation/sleep batteries; caps/rlimits/creds/
jails; VFS big lock + FSSTRESS + fsck; UDP/ARP/
DHCP/DNS + EMFILE pool + waiter matrix + wire
corpus + demux flood; thread-pool exhaustion
(THREX burst=15 exact math); cross-subsystem mix
(XSUB 40/40); trace seq/fetch/panic-tail; runtime
accounting invariant; 128B shell + fuzz; fbcon
console + CLI manual; 50-marker gates + 7 corpora
+ lint + gate driver. Evidence locations are in
the matrix; nothing here is claimed beyond its
gate.

## 5. Missing or unverified capabilities

~55 ABSENT rows (matrix): the entire crypto/trust
column (0/6); TCP/IPv6/TLS/routing/firewall; real
NICs; AHCI/NVMe; journaled RW FS; flash strategy;
FS crypto; mmap/COW/guards; NX/ASLR/KPTI/SMEP;
SMP; PI; ARM64/UEFI/ACPI/DT; USB/I2C/SPI/GPIO/
WiFi/BT/watchdog/power; pipes/shm/signals;
POSIX (9-function libc); SDK beyond x86-32;
services/config/metrics/remote-admin; packaging/
SBOM/signing; OTA/A-B/rollback; identity/
attestation; fleet/backup/crash-pipeline;
containers; disclosure process (SECURITY.md
missing); x86-64 kernel. UNVERIFIED (methodology
cannot establish): RT latency bounds; leak-
freedom across soaks; heap-exhaustion behavior.
Fresh doc-freshness findings (not edited, noted):
README still says "no shell, no filesystem";
ARCHITECTURE.md/ABI-STRATEGY.md are Phase-0
intent with divergences (x86-64-primary claim,
"no numbers frozen", hybrid direction vs
monolith reality, master-spec references) —
reconciled in ARCHITECTURE-STRATEGY.md, originals
left as history.

## 6. Strategic product recommendation

PRIMARY C (secure edge/IoT infrastructure OS):
v0.5 already looks like its prototype (headless,
bounded, NAT nets, seeded images, serial diag,
tiny TCB); missing pieces are large but ordered
(C needs ARM64/TCP/TLS/crypto/FS/OTA/fleet).
SECONDARY D as a workload profile inside C
(strict-prio seed + tick timebase; needs latency
proof + PI + certification evidence). First
product shaped as E (headless appliance, §9 of
the brief: all nine requirements genuinely
necessary — drop any one and it is not
deployable). REJECT A (desktop: zero leverage,
10,000-engineer treadmill) and B-primary
(server: ecosystem gravity; revisit via E only).
Narrow competitive objective: the deterministic,
attested, remotely lifecycle-managed edge node
with proof-carrying releases — testable, unlike
"surpass Linux."

## 7. Dependency findings

Graph + rationale: ROADMAP §6 (mermaid, 6 layers).
Load-bearing edges: isolation before sandboxing; M1
abstractions before ARM64; crypto+RNG before
anything authenticated; crash-consistency before
OTA; trust anchors before OTA; quotas before
untrusted workloads. Corrections to the brief's
candidate list: driver lifecycle/DMA policy must
precede the driver ecosystem (added); SBOM/
provenance precedes TCP/crypto adoption (added);
x86-32 end-of-support named at M5 (added).
Parallel-safe tracks: TCP vs FS-journaling vs ARM
bring-up (disjoint trees, shared gates).

## 8. Security findings

(a) v0.5 controls re-confirmed by search (38 copy/
validate sites; zero strcpy/sprintf; T8 vectors on
record). (b) THREAT-MODEL revised: 8 attacker
profiles (P1–P8), 6 trust boundaries, 15-row
threat-to-control matrix with testable acceptance
per row; HAVE rows gate v0.5, ABSENT rows gate
their milestone. (c) Five load-bearing assumptions
named (§5 of the model). Challenge list headed by single-executor + benign-firmware.
(d) Gaps with owners: disclosure process missing
(SECURITY.md cited, absent) → M1; SBOM/checksums
→ M0/M6; IOMMU/NX/ASLR → M2; signatures/rollback
→ M6. (e) Language discipline: "verified" means
tested, never proved — the model says so
explicitly for M7's assurance arguments.

## 9. Major architecture decisions

Retain monolith with seams (vs microkernel
rewrite — evidence: proof corpus bound to current
structure); abstractions-then-ARM64; x86-64
product before ARM64, x86-32 validation-only;
adopt TCP/crypto/FS (never hand-roll); A/B OTA;
POSIX-subset profile; processes+caps+quotas
before containers; measured RT, two classes;
freeze ABI with versioning; time-based support
lifecycle. Each is a PROPOSED (unratified) ADR in
docs/adr/proposed/ (14 files); the kernel-arch
one conflicts with ADR-0001's hybrid direction
and is marked as requiring explicit review.

## 10. Proposed milestones

M0 preserve (repro + SBOM seed) → M1 contracts
(threat/ABI/locks/DMA/response) → M2 isolate +
execute (NX/ASLR/mmap/SMP re-audit/RT measure) →
M3 crypto + transport (CSPRNG/vetted-crypto/TCP/
IPv6/TLS hostile-first) → M4 storage + drivers
(block abst/AHCI/NVMe/one journaled FS/flash) →
M5 ARM64 + hardware (one board, UEFI/DT, buses) →
M6 deploy + lifecycle (sign/SBOM/A-B OTA/rollback/
identity/attestation/telemetry) → M7 product
readiness (freeze, soak with growth metrics,
hostile-update ten, review) → M8 portability
(POSIX-subset, packaging, SDK). Full per-
milestone fields (objective → exit → residual)
in ROADMAP §7; gates policy in §9.

## 11. Prioritized blockers

Foundational (no downstream starts): arch/timer/
irq abstractions; ABI freeze; disclosure process;
driver/DMA policy; SBOM seed. First-product set:
CSPRNG, vetted crypto, TCP/IPv6, hostile-first
TLS, journaled FS + crash rig, A/B OTA, signing,
one ARM board, NX/W^X/ASLR, SMP re-audit.
Scored H/M/L with rubric in ROADMAP §8 (judgment,
not measurement — labeled as such).

## 12. Human decisions required (no implementation
in these areas until ratified)

1. Retain monolith vs microkernel/hybrid (conflicts
   ADR-0001 — explicit review mandatory).
2. Platform order: x86-64 product first, then ARM64;
   x86-32 validation-only with named end-of-support.
3. TCP stack source (adopt-PTL shortlist + license).
4. Crypto library source + entropy architecture.
5. Production filesystem choice (extend vs adopt).
(Plus, before their milestones: POSIX scope, first
board, isolation model. Seven total; the first
five gate all code.)

## 13. Files created or modified

Created: docs/ROADMAP.md, docs/ARCHITECTURE-
STRATEGY.md, docs/PRODUCTION-READINESS-MATRIX.md,
docs/NOVA-POST-V0.5-ARCHITECTURE-AUDIT.md (this
file), docs/adr/proposed/ (14 files). Modified:
docs/THREAT-MODEL.md (Phase-0 sketch revised in
place with history note; original in git history).
NOT modified: any kernel/userspace/tool/test
source, any ratified ADR, ABI contracts, the v0.5
release manifest, README, CHANGELOG.

## 14. Implementation-code change statement

Zero. Verified by `git status --short` (only the
files in §13 plus the pre-existing untracked user
screenshot) and `git diff --stat` scoped to
docs/. No syscall, struct, trace-format, or ABI
touch of any kind.

## 15. Final verdict and recommended next action

Verdict: PASS WITH OPEN DECISIONS — the audit is
complete, the baseline is intact, the strategy is
argued, and the 12 proposals are properly marked
unratified; the 7 decisions in §12 are genuinely
open and must precede implementation. Recommended
next action: review and ratify (or reject with
reasons) the 5 code-gating proposals, then
authorize M0+M1 as the first work package (both
are contracts/provenance, no product code). Do
not authorize M2+ implementation until M1's
contracts are review-signed. Stop here.
