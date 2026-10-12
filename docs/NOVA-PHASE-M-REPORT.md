# NOVA OS — Phase M Report: Decision Gate & Portability Foundation

## 0. Position

HEAD `0b5da64`, tree as §1 of the brief plus this
phase's four new documents (all uncommitted, no
push). v0.5 baseline (`59ea07b`, ancestor: true)
untouched — zero implementation changes (verified
§13). This phase settles direction, specifies
contracts, and researches dependencies. It
authorizes no code.

## 1. Decision Gate A — Kernel architecture: B, as ADR-0001's implementation

Recommendation: **B — modular-monolithic
evolution** (one privilege domain; explicit tested
boundaries in fixed order; userspace only for NEW
crash-safe services with measured IPC budgets).
Preserved: the entire proof corpus, lock/IF
discipline, review culture, one-domain debuggability.
Explicit next: arch extraction, HAL, driver
lifecycle/registry, service seams. Incremental:
every boundary lands additive with gates. Breaking
if attempted: privilege-domain splits of PROVEN
subsystems (sched, VFS, sockets) without a
fault-injection case. Cost of one domain kept:
W3 (drivers ring-0) until late — priced openly,
bounded by the driver/DMA policy. Later evidence
that could overturn: certification demanding
isolation, a boundary-value injection study, or
third-party-driver product need.

ADR-0001 conflict analysis (precise):
- Original (accepted Phase 0): hybrid/modular —
  privilege only where it measurably pays; FS/net-
  upper/graphics isolated "where practical";
  monolith rejected (TCB/coupling), microkernel
  rejected (IPC burden, small team).
- Proposed: modular monolith with the same review
  discipline (boundaries + ring0-justification)
  and deferred userspace extraction.
- Incompatibility: PARTIAL, not total. The v0.5
  tree violates ADR-0001's isolation half today
  (everything ring-0) — that violation predates
  this phase and is recorded as W3, not created
  by the proposal. The proposal satisfies the
  discipline half now and sequences the isolation
  half (boundaries → policy → extraction on
  requirement). Claiming "monolith forever" WOULD
  conflict; nobody proposes it.
- Retaining ADR-0001 unchanged: every M2+ addition
  passes "does this need ring0?" with an isolation
  path — compatible with B, costs review effort.
- Changing it: only needed if the team wants
  monolith-forever or microkernel-now; neither is
  recommended, so NO amendment proposed.
- Migration: none. B is filed as the execution of
  ADR-0001's consequences (reassessment appended to
  the still-unratified proposal; ADR-0001 itself
  untouched).
Status: RECOMMENDED — AWAITING APPROVAL (interprets
a ratified ADR; human sign-off required; gate for
implementation: BLOCKED until then).

## 2. Decision Gate B — CPU architecture and portability: x86-64 first, contracts now

Decisions (all RECOMMENDED — AWAITING APPROVAL):
- x86-32 stays as VALIDATION-ONLY: CI boots it,
  gates stay green, no new product features after
  M5; end-of-support named with the first 64-bit
  release (condition: 64-bit gates at parity).
- x86-64 first because: closest tree/rig/drivers/
  toolchain; unblocks NX/SMAP/PCID/IOMMU that
  32-bit cannot carry; cheapest abstraction proof.
- ARM64 prerequisites: M1 abstractions green with
  zero-delta x86, named board, GIC/timer/DT
  designs, toolchain pinned. No code before that.
- RISC-V justified when: QEMU-virt port proves
  abstractions honest AND (board exists OR the
  port costs <2 weeks) — milestone, not promise.
- Build system multi-arch: today's one-driver
  extends per-arch (arch flag + per-arch link);
  no new build tech needed, only discipline.
- Arch contracts required before implementation:
  the 12-item inventory (FOUNDATION-CONTRACTS §F:
  cpu, MMU, exceptions, ctx, barriers, boot, mem
  map, IRQ, timers, firmware, DMA, power — each
  with owner/interface/failure/test).
No arch directories or stub code created (per
brief: no placeholders implying portability).

## 3. Decision Gate C — Networking: lwIP shortlist, boundary before code

Comparison (researched 2026-10-09; statuses are
findings, not endorsements):

| Candidate | License | Maintained? | Fit notes |
| lwIP 2.2.x | BSD (permissive) | YES: 2.2.1 Feb 2025, active fixes through 2025–26, CI, RTEMS/space use | full TCP/IPv6, ~40 KB ROM class, socket API; PREFERRED |
| FreeBSD-stack port | BSD | YES (upstream) | Darwin precedent; fullest TCP but large + POSIX-tied; fallback if lwIP fails eval |
| CycloneTCP | GPL/commercial dual (VERIFY before use) | claims active; NOT independently verified here | evaluate only after license check |
| Hand-roll | n/a | n/a | REJECTED (review depth absent) |

Unresolved (must answer before M3 code): exact
pin (STABLE tag), Heap-vs-pool alloc mapping to
NOVA (pool-bounded? quota?), threading model
(tcpip-thread vs NOVA pump — pick one, no both),
fuzz corpus availability, ND/RA hardening tests.
Integration boundary: NIC interface → lwIP netif
→ NOVA socket translation shim (quotas enforced
in the shim, never trusting stack internals);
parsers stay NOVA-side where they already win.
Test plan: state-machine fuzz, retransmit/congestion
vectors, exhaustion (PCBs/queues), hostile-peer
suite, dual-stack parity. No download/vendoring
done. Status: RECOMMENDED — AWAITING APPROVAL.

## 4. Decision Gate D — Crypto: shortlist with a dated caveat

Comparison (researched 2026-10-09):

| Candidate | License | Maintained? | Fit notes |
| Mbed TLS 4.1 LTS | Apache-2.0 OR GPL-2.0 | MIXED: LTS to Mar 2029, BUT maintainers exit TLS/X.509 Apr 2027 without new maintainers (announced Oct 2026); TF-PSA-Crypto stays maintained | PREFERRED with caveat: pin 4.1 LTS + reassess by Q1 2027 |
| BearSSL 0.6 | MIT | slow/single-author; beta-labeled; no TLS 1.3/DTLS | fallback eval only; license-clean |
| wolfSSL 5.9 | GPLv2 + commercial | YES, active, TLS 1.3 | copyleft blocks Apache dist → commercial-license decision required; evaluate iff buyer funds it |
| Monocypher | CC0/public-domain style | small, steady | primitives only (no TLS) — useful SEAM test, not the stack |
| Hand-roll | n/a | n/a | REJECTED absolutely |

CSPRNG architecture (PROPOSED): layered — per-
board entropy inventory (x86 RDRD/RDSEED +
jitter, ARM RNDR, virtio-rng, TrustZone; NEVER a
single source, NEVER blind trust) → seed a
vetted DRBG → health tests at boot + continuous;
startup with insufficient entropy BLOCKS crypto
services (refuse, never weak keys). Key lifecycle:
generate-on-device preferred, sealed storage
where HW allows, rotation + revocation from day
one, secrets zeroed + never logged. TLS needs:
1.2 minimum, 1.3 preferred, hostile-first cert
validation corpus, DTLS only if UDP product
needs it. Acceptance: known-answer vectors +
wycheproof-class suites in-guest + misuse-proof
seam review. Status: RECOMMENDED — AWAITING
APPROVAL (library pick + entropy design).

## 5. Decision Gate E — Filesystem: littlefs for flash, disk FS still open

Comparison (researched 2026-10-09):

| Candidate | License | Maintained? | Fit notes |
| littlefs | BSD-3 | YES: active (2026 issues/PRs), 6.7k stars, Mbed heritage | power-loss resilient, wear-leveling, bounded RAM — PREFERRED for SPI/NOR-flash edge |
| FATFS (Elm Chan) | permissive (1-clause style) | steady | interchange only, no journaling — keep for SD/USB transfer, not system disk |
| NOVA-FS grown w/ journal | Apache-2.0 (ours) | n/a (new work) | keeps VFS story; pays design risk; evaluate vs littlefs on crash rig |
| ext4 subset port | GPL-2.0 | YES (upstream) | REJECTED for product: copyleft + complexity dwarf the team |
| Hand-rolled journal | n/a | n/a | REJECTED as first choice (adopt-then-extend only) |

Honest caveats recorded: littlefs has documented
unsynced-write semantics (zero-length files after
power loss before sync/close — mitigate by
rename-pattern discipline + app rules); littlefs3
in development (do NOT track unstable); littlefs
targets small flash, NOT gigabyte disks — the
x86-64 appliance disk FS is a SEPARATE open
question (options: read-only system image +
littlefs data partition is the leading shape;
evaluate, don't assume). Staged strategy: (1)
littlefs for flash-edge product; (2) disk story
resolved with the x86-64 appliance decision.
Block contract: erase/program-size abstraction,
flush semantics (cache→media ordering points),
torn-write model, per-media qualification.
Recovery: mount-after-chaos suite, fsck-equivalent
role defined, corruption injection rig. Status:
RECOMMENDED — AWAITING APPROVAL (flash pick;
disk pick explicitly deferred with options).

## 6. Hardware abstraction plan (approved-direction draft)

Five layers per the review §5 (core / arch/<isa> /
plat/<board> + boot producers / drv families /
plat manifests), each boundary carrying: API +
owner, init order, lifetime, concurrency + IRQ
bans, errors, DMA/coherency assumptions, stub
testability, documented deltas, migration path.
Driver framework in stages 1-2-3 (lifecycle →
registry/matching → power/recovery); unloadable-
or-not default NO; third-party drivers refused
until signed+sandboxed boundary exists. No
framework code until the first production board
is named — the plan is reviewable without being
built. Status: RECOMMENDED — AWAITING APPROVAL
(as part of the portability-boundaries proposal).

## 7. M0 + M1 plan status

M0 (REPRODUCIBLE-BUILD-PLAN.md + SOFTWARE-
PROVENANCE-PLAN.md): commands pinned to today's
driver; hash set defined (kernel/stage1/blobs/
raw); CHECKSUMS file + SBOM generation + font
provenance + header sweep are M0 actions, all
currently ABSENT and named as such. M1
(FOUNDATION-CONTRACTS.md §§A–F): threat deltas,
ABI policy, locking+SMP caveat, DMA contract,
SECURITY.md contents (placeholders [HUMAN], no
invented contacts), 12 portability contracts.
Both PROPOSED; M0 exit = two-host identical
rebuilds; M1 exit = review-signed contracts.

## 8. Review gate classifications

- APPROVED BY EXISTING ADR: none in this phase
  (nothing ratified here; ADR-0001 stands
  unamended — the Gate A finding is an
  interpretation, not a change).
- RECOMMENDED — AWAITING APPROVAL: modular-
  monolith direction; x86-64-first + x86-32
  validation policy; lwIP shortlist + boundary;
  crypto shortlist + CSPRNG design; littlefs for
  flash (+ deferred disk pick); HAL five-layer
  plan; M0/M1 contracts and plans; ABI policy;
  DMA contract; disclosure contents.
- BLOCKED BY MISSING EVIDENCE: ARM64 board (no
  board → no GIC/timer/power claims); disk-FS
  pick (needs appliance decision); WolfSSL-or-
  BearSSL fallback order (needs maintainer-risk
  call + buyer input); formal RT bounds (needs
  measurement rig).
- DEFERRED: RISC-V product support; containers;
  full Linux ABI; KEXT-style modules; second
  board; radio stacks.

## 9. Unresolved evidence gaps

Board choice; exact TCP pin + alloc model;
crypto pick confirmation pre-Apr-2027;
disk-FS decision; entropy per-board analysis;
certification target for RT; SBOM tooling run;
font provenance (M0 action). Each names its
resolving action and owner-milestone; none was
filled by assumption.

## 10. First implementation phase after approval

M0 (preservation + provenance: CHECKSUMS, SBOM
seed, font resolution, two-host rebuilds) CON-
CURRENT WITH M1-contracts-writing (threat deltas
review-signed, ABI lint, portability extraction
DESIGN — code extraction itself is M1-exit-gated,
not M1-free). First CODE change overall: arch/
timer/irq extraction with identical serial bytes.
Estimated entry criteria: 5 approvals in §11 of
the brief + ratified M0/M1 plans. No Phase N
prompt generated here.

## 11. Verification (brief §13)

1. HEAD `0b5da64`, tree as §0: verified.
2. No implementation code modified: verified
   (status shows docs/ only; manifest + ABI
   untouched — not even opened for edit).
3. Doc diff inspected: 4 new files + 0 modified
   (this phase); prior-audit files preserved.
4. Lint: `python tools/nova.py lint` → clean
   (ADR 0001..0080, 1 tolerated warning) — safe,
   read-only, no files rewritten.
5. Live VM untouched (no boot/power/test runs).
6. VM suite NOT re-run (stated; last green on
   record at HEAD).
7. Manifest + ABI unchanged: confirmed.
8. All proposals unratified: confirmed (14 prior
   + 0 new this phase — this phase added none,
   only referenced; numbering convention intact).
9. Commands + outcomes recorded here.
Stop conditions: none triggered (no unexpected
tree changes; no decision lacked evidence — gaps
marked BLOCKED/DEFERRED instead; no license
status invented — CycloneTCP explicitly
unverified; no user work disturbed).
