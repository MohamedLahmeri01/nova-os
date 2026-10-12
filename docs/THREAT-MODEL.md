# NOVA OS — Threat Model (post-v0.5 audit revision)

Status: normative for design reviews. **Proposed controls are
not implemented.** Verified v0.5 controls cite evidence;
everything else is a requirement on future milestones.

History note: this file began as a Phase-0 sketch (actors
1–7, out-of-scope list, per-interface checklist). That
content is preserved in git history. This revision keeps
its actor numbering, re-tests each assumption against the
v0.5 tree, and adds the deployment threats a Phase-0
VM-only model could ignore.

## 1. Assets

Kernel code/data, page tables, scheduler state, trace
ring, credentials (uid/gid/caps/rlimits), handles (fds,
sockets, windows), filesystem contents + integrity,
network availability, boot chain (stage1→kernel→init),
build chain (toolchain, scripts, corpora), update
artifacts (future), device keys (future), fleet
telemetry (future).

## 2. Trust boundaries (as built)

1. Userspace ↔ kernel: INT 0x80 gate; all payloads via
   `validate_usermem` + centralized copies (ADR-0066).
2. Driver ↔ device: PCI BARs + virtio rings + ATA status
   are untrusted input — but the driver itself is
   ring-0 (no boundary; gap W3).
3. Image/data ↔ parser: wire frames, FS images, DNS
   replies, DHCP offers are untrusted bytes (bounds-
   first parsing, corpus-gated).
4. Boot stages: stage1 trusts BIOS-provided E820/map;
   kernel trusts stage1's `NOVOSBOOT` struct (validated:
   T-series boot-info checks — see boot tests).
5. Serial console: physical-console privilege (regs/CRx
   MAY appear; policy, not enforcement).
6. Build host ↔ image: NO verification today (no
   checksums/SBOM/signing — gap).

## 3. Attacker profiles

P1 Malicious unprivileged application (maps to old
actors 1, 3, 5): hostile syscall args, sibling-thread
tearing, resource hogging, parser-probing via fs/net
syscalls. V0.5 controls: validation-first fail-closed
ordering (VERIFIED: T8 vectors), snapshot copies
(ADR-0066), rlimits + caps (VERIFIED: RLIM/CAPS-OK),
immutable mappings (VERIFIED). Residual: no NX/ASLR
(W2) — any bug is more exploitable than on a
hardened kernel; no sandbox beyond uid/caps/jail.

P2 Compromised network service / remote unauthenticated
network attacker (old actor 3-part, new): malformed
frames, DHCP/DNS spoofing on NAT, socket-pool races,
floods. V0.5 controls: bounds-first parsers + corpus
(VERIFIED), pool lock + waiter matrix (VERIFIED:
SOCKBLK/NETSTRESS-OK), no reassembly (attack surface
refused by design). Residual: no firewall/quotas/TLS;
ARP trusts first valid reply (documented).

P3 Malicious/buggy driver or device (old actors 2, 6):
crafted virtio descriptors, BAR lies, ATA status
games, DMA writes (no IOMMU). V0.5 controls: ring
id/len validation + EIO paths (VERIFIED), BAR range
checks, bounded ATA spins. Residual: W3 OPEN —
drivers are ring-0; a driver bug IS a kernel bug;
DMA can write anywhere (W5).

P4 Malicious filesystem image / update package (old
actor 4, extended): crafted FAT/EXT/NOVA-FS bytes,
fsck bypass, graft confusion. V0.5 controls: mutant
manifests + driver+fsck replay (VERIFIED: 7+7+9),
EROFS-gated graft writes. Residual: no signed-image
verification at all; fsck is integrity, not
authenticity.

P5 Local user attacking neighbors: jail escape via
`..`/graft paths, fd/socket starvation, keysnoop
via shared console. V0.5 controls: jail floors +
graft invisibility (VERIFIED: JAIL-OK), per-
process fds, EBUSY single-waiter. Residual:
TOCTOU-on-paths accepted semantics; no per-user
quotas beyond rlimits.

P6 Physical attacker with device access: cold boot,
flash extraction, console reads, peripheral abuse.
Controls today: NONE (out of scope since Phase 0;
still out of scope for v0.5). Required later:
encrypted storage, secure boot, tamper response
(hardware-dependent; see M6).

P7 Supply-chain attacker (old actor 7): toolchain,
Python deps, corpora, CI. Controls today: pinned
mingw-w64 + stdlib-only build driver (PARTIAL);
no SBOM, no artifact checksums, no signed
toolchain (gap). Required: provenance + SBOM
(M0/M6), RESPONSE process (the referenced
SECURITY.md does not exist — gap, see §5).

P8 Compromised update server / interrupted power
during update / corrupted storage: NO update
mechanism exists, so the threat is currently
vacuous — it becomes the dominant threat the
day OTA lands (M6 must answer all three before
first deployment).

## 4. Threat-to-control matrix (acceptance criteria are tests)

| Threat | Prevention (required) | Detection | Recovery | Residual risk | Evidence to claim it |
| hostile syscall args | validate-first + snapshots (HAVE) | FAIL-tag batteries | errno, no panic | W1 tearing accepted | T8 vectors + 23/23 |
| sibling-thread races | locks + IF discipline (HAVE) | stress + THREX/XSUB | bounded refusal | coarse VFS lock | FSSTRESS/NETSTRESS/XSUB |
| malformed packets | bounds-first parsers (HAVE) | corpus replay | drop + counter | ARP-first-trust | 13+15 seeds + flood |
| malformed FS images | mutant manifests (HAVE) | driver+fsck replay | EIO/EROFS | no authenticity | 7+7+9 rows |
| socket exhaustion | pool cap + EMFILE (HAVE) | churn tests | close→open | no per-user quota | SOCKBLK-OK |
| thread exhaustion | pool cap + refusal (HAVE) | burst test | reap→create | fixed 16 (tunable later) | THREX-OK |
| trace overflow | overwrite-oldest + seq gaps (HAVE) | 600-emit proof | fetch continues | 256-event retention | TRACEFETCH-ADV-OK |
| driver bug → ring0 | driver isolation (ABSENT) | driver test harness | reboot | W3 full compromise | harness + IOMMU policy (M2/M4) |
| DMA attack | IOMMU (ABSENT) | — | — | W5 total | IOMMU on capable HW (M2) |
| no-exec bypass | NX (ABSENT) | — | — | user data executes | NX enable + tests (M2) |
| KASLR bypass n/a | ASLR (ABSENT) | — | — | deterministic layout | ASLR design + tests (M2) |
| forged update | signatures (ABSENT) | verify-before-apply | rollback | total on accept | signed pipeline tests (M6) |
| power-loss mid-update | transactional apply (ABSENT) | journal/state machine | A/B rollback | bricked device | power-cut test rig (M6) |
| key theft (device) | secure element/TPM (ABSENT) | attestation | revocation | cloned identity | provisioning docs (M6) |
| supply-chain poison | SBOM + pinned/hash deps (ABSENT) | provenance check | rebuild + rotate | silent backdoor | SBOM + RESPONSE process (M0/M1) |

Reading rule: HAVE rows gate v0.5; ABSENT rows gate
their milestone. A row is claimed only with the
evidence in the last column, reproduced — never by
the existence of this table.

## 5. Security assumptions (load-bearing; challenge these)

1. Single executor (BSP-only). Nearly every v0.5
   argument (lock-free paths, cli-sections, park
   discipline) assumes it. SMP invalidates them all
   at once — that is why SMP has its own milestone
   with a full re-audit gate.
2. Benign firmware + devices. BIOS, 8042, PIC, PIT,
   ATA, virtio-device are trusted to behave like
   hardware (bounds-checked, not verified).
3. Physical console is privileged. Serial/VGA may
   carry regs/CRx; anyone who can read them already
   owns the box.
4. Deterministic layout is acceptable. No ASLR means
   every address is knowable — the model compensates
   with validation, not secrecy.
5. Response process exists on paper only. The Phase-0
   text cites `SECURITY.md` and "master spec §37";
   neither exists in-tree. Creating the disclosure +
   patch-lifecycle process is an M1 deliverable, and
   M7 cannot pass without evidence it was exercised.

## 6. What this model does NOT cover (yet)

Microarchitectural side channels (out of scope since
Phase 0, reaffirmed: no SMT, no shared cores in the
target profile, but no measurements either);
encrypted-filesystem secrecy (no FS crypto design);
formal verification of any property (no proofs exist;
"verified" in this repo always means tested, never
proved — keep that distinction sharp when M7 asks
for assurance arguments).
