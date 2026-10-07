# NOVA OS — Security Model (v0.4 Phase A, normative)

Status: normative for design reviews (succeeds the Phase-0
THREAT-MODEL.md checklist; that file remains the per-review
checklist, this file is the architecture record). Claims
below are verified against source at v0.3+ (commit
`1023ffa`); anything aspirational is labeled FUTURE.

## 1. Trusted components

```text
Component            Trust basis                             Failure if wrong
bootloader (stage1/2) measured by nothing; 500B audited, build-asserted size  early code runs before any check
kernel text          mapped read-only (vmm PT0, selftest -7); reviewed     arbitrary ring-0 on .text write bug
arch backends        tiny, reviewed; no raw asm elsewhere                  privilege/IRQ/atomic breakage
PMM/VMM/heap         tags, magic, canaries, trim-exactness, fuzz (T7)      everything (all memory flows through)
scheduler            overlap/starvation proofs, switch counter             liveness + isolation of threads
syscall dispatch     table-driven, validation-first, 20k fuzz (T8)         the userspace boundary itself
drivers (ata/pci/   ring-0, TRUSTED (no isolation — see §7 weakness W3)   full compromise on driver bug
  virtio/fb/vga)
FS implementations   parsers fuzzed + corruption-injected (T9–T13)        crafted image → kernel bug (see W4)
net stack            rogue mutants gated (T14), demux bounds-checked      crafted packet → kernel bug (see W4)
```

## 2. Untrusted inputs

```text
Input                 Entry point              Validation (implemented)
userspace pointers    every syscall arg        validate_usermem: USER_BASE..USER_END,
                                               no-wrap, ≤8192B range, per-page
                                               present/user(/writable); centralized
                                               copy_from/to_user (ADR-0066)
userspace lengths     same                     capped per call (PRINT 1024, files 64KB,
                                               sock MTU, winstat 93×44); overflow-checked
userspace strings     paths, titles, DNS names NUL-scan with cap (copy_user_path),
                                               grammar checks (DNS), truncation documented
FS images/sectors     ata → fs drivers         parsed-not-assumed: magic, geometry,
                                               step caps, rec_len progress, crc32c
                                               (NOVA-FS), 4-mutant fsck gate
network packets       virtio ring → parsers    eth/IP/UDP/DHCP/DNS length + checksum
                                               + grammar checks; 11-seed corpus replay
DHCP/DNS responses    net_udp_recv paths       XID/chaddr/magic/ID match, bounds on
                                               every option/field, rogue mutants gated
device-provided data  PCI BARs, virtio rings   BAR range checks (BAR2-only for virtio),
                                               ring index/len bounds, used-entry validation
future DMA buffers    NONE YET                 no DMA buffers exist (BMIDE uses kernel
                                               pages); IOMMU absent — see W5
```

## 3. Security boundaries

Format per boundary: trust · validation · lifetime · permissions · failure · concurrency · ownership · weaknesses.

### userspace → syscall ABI
- Trust: NONE (actor 1). Every arg hostile until validated.
- Validation: `validate_usermem` + centralized copies + struct snapshots; scalars range-checked; caps checked first (fail-closed order).
- Lifetime: snapshot-at-entry; kernel never holds user pointers across sleep/block (bounces are kernel-owned).
- Permissions: CAP_NET/SPAWN gates; rlimits; uid/gid/mode; jail floors.
- Failure: `-errno` (EFAULT/EINVAL/EPERM), never panic, never fault-escape (mappings immutable — ADR-0066 invariant).
- Concurrency: sibling-thread tearing accepted (valid-input semantics); NO cross-thread mapping changes exist (no munmap/mprotect). See W1.
- Ownership: user owns its pages; kernel owns snapshots/bounces.
- Weaknesses: W1 (TOCTOU residual), W2 (no NX/ASLR deepens any bug).

### userspace → memory manager
- Trust: NONE. `addrspace_map` refuses kernel half outright (`virt < USER_BASE` panics as programmer bug — kernel callers only), RLIMIT_FRAMES ceiling, no unmap API (count only grows).
- Lifetime: maps live with the addrspace; destroy frees PTs (mapped frames caller-owned — spawner frees).
- Failure: `-ENOSPC` at ceiling; malformed args panic (kernel bug, not user input — no user reaches this except via validated syscall paths... note: only selftests call it directly today).
- Concurrency: single executor; no locking (SMP debt).
- Weaknesses: W2 (no guard between user mappings and MMIO except USER_END cap at 3GB — adequate, audited).

### userspace → filesystem
- Trust: NONE (actor 4 for images; actor 1 for paths). Paths snapshotted; lookups bounded; graft writes EROFS-guarded; write-through ordering (OOM before mutation).
- Lifetime: refcounts + unlinked flag; fds per-process, EBADF on misuse; jail roots held.
- Permissions: triplets + root bypass, checked at open/create/mkdir/readdir, fail-closed order.
- Failure: errno (ENOENT/EACCES/EROFS/ENOSPC/ENOTEMPTY/EBUSY); fsck for NOVA-FS.
- Concurrency: single-threaded FS users; no locking (SMP debt).
- Weaknesses: W4 (parser depth — see §5), no symlinks yet (absent, not vulnerable), TOCTOU on paths accepted (lookup-at-open semantics).

### userspace → networking
- Trust: NONE. Ports/lengths validated; payloads MTU-capped; single-slot overwrite (no queue to flood); waiter EBUSY (no pile-up).
- Lifetime: sockets per global pool with generations; close/rebind fail waiters EBADF.
- Failure: errno (EADDRINUSE/EBADF/EAGAIN/EINVAL/EIO/EHOSTUNREACH).
- Concurrency: waiter matrix proven (lost/theft/close races gated).
- Weaknesses: no per-socket quotas (flood = overwrite, accepted); no firewall.

### device → driver
- Trust: NONE for descriptor content (actor 2/6-lite). Virtio ring id/len bounds-checked per drain; used-entry validated (EIO path, buffer kept posted); PCI BARs range-checked; ATA status bits polled with timeouts (no unbounded waits except the 2M documented bound).
- Lifetime: posted RX pages owned by driver, reposted after copy.
- Failure: -EIO/ENOSPC/ENODEV; degraded modes (PIO fallback, MOUSE-ABSENT, polling-only virtio).
- Concurrency: ring drain under IF-saving lock; drill under held lock.
- Weaknesses: W5 (DMA without IOMMU), W6 (no MSI; shared PIC lines trusted).

### driver → kernel
- Trust: FULL (ring-0, no isolation) — documented gap, not a boundary. See W3.

### filesystem data → VFS
- Trust: NONE. Drivers return parsed-or-error; VFS never interprets raw bytes (materialize-then-own pattern; opener-owned foreign views).
- Weaknesses: W4.

### network packet → protocol parser
- Trust: NONE. Length-first parsing, checksums, grammar, jump caps; corpus replay gates 11 shapes.
- Weaknesses: packet-shape fuzz beyond crafted vectors is open (Phase L).

## 4. What is explicitly NOT claimed

No NX enforcement (no bit), no ASLR (deterministic by design), no
KPTI (supervisor-only maps suffice for access), no IOMMU, no secure
boot, no MAC, no audit log, no sandbox escape testing, no driver
isolation, no TOCTOU-proof paths beyond snapshots, no timing-channel
defense.

## 5. Weakness register (live list — update on every fix/find)

Re-triaged 2026-10-06 (v0.5 Phase A). Statuses: OPEN /
MITIGATED / CLOSED / DEFERRED (exactly one each).

```text
W1  sibling-thread data tearing — MITIGATED (unchanged).
    Snapshots hold everywhere (re-verified: 8 helper sites
    + struct snapshots + NUL-scan paths). No new sibling-
    writer surface in v0.4 (bench file churn runs boot-
    threaded, sequential). Residual accepted semantics.
W2  mapping boundary — MITIGATED (was: open with imprecise
    wording). Code-patch closed (RO text enforced +
    runtime asserts); user access to kernel maps verified
    impossible (U/S=0, grep + MEMMAP grid). Residual:
    user data executes (no NX bit exists) + no
    randomization. See §14 of ADR-0067.
W3  ring-0 drivers unisolated — OPEN (unchanged). No v0.5
    work isolates drivers; virtio/ATA/fb stay fully
    trusted. Bounds-checking (rings, BARs, ATA status)
    is the only control.
W4  parser depth — MITIGATED (was: open). Corpus grown:
    11 wire seeds + 10 shapes + queue flood (net), FS
    manifests (7+7+9 rows, driver + fsck), shell T23
    (16 lines + 3k soup, strtoi UB fixed). Remaining:
    packet-shape breadth, concurrent-FS (needs locks
    first), shell syntax that doesn't exist yet.
W5  DMA without IOMMU — OPEN (unchanged, out of scope
    with reason: VBox-only threat, BMIDE uses kernel
    pages, no userspace DMA buffers exist).
W6  PIC shared lines, no MSI — OPEN (unchanged). Routing
    trusted; cascade history says stay vigilant; MSI is
    future work with the APIC milestone.
W7  trace/stats disclosure surface — DEFERRED (new).
    No userspace reader exists yet (panic dump goes to
    serial only — physical-console privilege, no new
    exposure). Trigger: the fetch ABI (v0.5 Phase C)
    must project fields (counters, tids, ports), never
    memcpy structs: thread/process structs carry kernel
    addresses (sp, stack_base, cr3, user_stack) and the
    FAULT event carries the faulting address (possibly
    a kernel address a user probed). Requirements
    recorded in Phase A report; Phase C enforces.
W8  FS concurrent use unsound — OPEN (new, found Phase A).
    Refcounts plain ++/--, node buffers + fd offsets
    shared, zero locking anywhere in fs/. Safe today
    ONLY because no two threads use FS concurrently
    (shell single-threaded; selftests sequential).
    Contract declared in ADR-0076: FS syscalls are
    single-threaded-only until locking lands; Phase O
    stress is gated on it. Not exploitable from the
    current userspace (one reader), so OPEN-not-P0.
```

W2 corrected 2026-10-06 vs the v0.2 audit's P1-3 wording: user
CANNOT read kernel mappings (U/S=0 faults at CPL3); the gap is
executability of user data + absence of randomization, not
readability. Precision matters in this file.
