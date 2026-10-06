# NOVA OS v0.2.0 — Architecture Audit

Date: 2026-10-06. Auditor: principal-architect session (automated inspection + full suite run).
Scope: entire tree at commit `6173e88` (post-v0.2.0, clean working tree), all 63 ADRs, all docs, full `nova test` baseline.
Method: read-only inspection (no code modified for this audit); every behavioral claim below was either read in source or observed in the baseline run. Speculation is marked as such.

## Baseline record

```text
commit:    6173e88dd04e3c5cbf7817657af2aa2a101c35c4 (clean tree)
version:   0.2.0 (VERSION file; banners verified on serial)
compiler:  gcc (mingw-w64) 15.2.0, flags -m32 -ffreestanding -nostdlib -Os -mno-sse -mno-mmx -Wall -Wextra
python:    3.11.2 (stdlib only in tooling)
hypervisor: VirtualBox 7.2.6r172322
host:      AMD Ryzen 5 5500, ~16 GiB RAM, Windows 11 64-bit
test VM:   NOVA-OS-CONSTRAINED, 2 CPU / 1024 MB, PIIX3/BIOS, NAT + virtio NIC, UART COM1 -> build/serial.log
suite:     21/21 PASS (T1-T21), ~3-4 min wall time (T5 ~40 s, T6 ~60 s guest boots, rest host-side seconds)
gates:     all boot markers present (NET-OK, DHCP-OK, SOCKBLK-OK, IRQPUMP-OK, KBDBLK-OK, WID-OK, CLICK-OK, ...)
```

## 0. One-page verdict

v0.2.0 is a coherent, honestly-tested 32-bit experimental OS: ~110 source files, ~35 syscalls, working userland, real wire networking, and a test suite that fails only on project bugs. The ADR log (63 entries) is the project's strongest asset — including reverted and declined work. The single most urgent issue is non-technical: **there is no LICENSE file** (ADR-0008 unratified; default is all-rights-reserved), so "open-source" cannot be claimed yet. The largest technical divergences from written decisions: **ADR-0002 (Rust-first) is silently violated** — the kernel is 100% C with zero `.rs` files — and the scheduler is strict-priority while ADR-0004 promises a fair default. SMP exists as bring-up only (APs park). No TCP, no signals, no IPC, no ELF, no NX/W^X, no CI. Recommended v0.3 (§36): license ratification, ADR-0002 supersession, syscall/TOCTOU audit, SMP locking foundations, scheduler fairness measurement.

---

## 1. Repository architecture

Kernel sources live in `kernel/`; userspace in `userspace/` (init+sh, hi, uname; libc in `userspace/libs/`); public headers in `sdk/`; tooling in `tools/nova.py` (~60KB, 12 commands) plus fixture/fsck scripts; tests in `tests/unit` (12 host tests) with `tests/integration` empty; docs in `docs/` (12 guides + 63 ADRs); `benchmarks/pmm` only. Empty scaffolding (0 files): `kernel/{core,ipc,sync,security,scheduler}` (the last duplicates `sched/` by name), top-level `drivers/* fs/ net/ scripts/ ci/ examples/ libs/*`. `build/ images/ dist/ vms/` are gitignored outputs present locally. Largest units: `fs/nova.c` 66KB, `net/net.c` 63KB, `syscall/syscall.c` ~62KB. Kernel binary ≈ 130KB (254 sectors).

## 2. Boot flow

`stage1.S` (500B, build-asserted ≤510) → INT13 LBA load of stage2 in ≤127-sector chunks (768-sector cap) → E820 map (24-byte stride, verified against firmware behavior) → A20 (BIOS + port 0x92) → VGA mode 13h select → flat GDT → 32-bit protected mode → `stage2asm` → `entry.c` → `pm_main.c`: PMM/VMM/heap → IRQ/PIT → selftests (each panics on failure) → scheduler → userspace spawn (`/bin/hi`, `/bin/uname` proofs) → demo → shell. APs: MP-table parse, INIT/SIPI, `ap_tramp.S`, self-check, print, park (`cli;hlt`) — no AP scheduling. Fault image (`kernel_fault.bin`, T6) proves the trap→panic chain via `ud2`.

## 3. Memory architecture

PMM behind one backend interface; bitmap won Phase 2a on measurements (buddy kept as parked, explicitly unimplemented alternative). VMM: classic 32-bit non-PAE paging (PAE triple-faults under VBox NEM — 9-boot elimination in ADR-0011; stays out until QEMU/long-mode proof). Layout: null page read-only, [0,4MB) 4KB pages with stack-guard page absent, 4MB large pages above (PSE), WP enabled. **No NX** (impossible without PAE), **no W^X, no COW, no demand paging** (ADR-0003 promised guard/NX/COW/W^X "from the start" — mostly unfulfilled; §26 P1-3). Heap: slabs 8–2048 + large runs, 1B frame tags, magic-checked free, always-on double-free walk, `kheap_trim` for exact restoration. Addrspace: one PD per process, kernel low mappings cloned (shared, never freed per-PD), user mappings ≥2GB (`USER_BASE 0x80000000`); validation ceiling `<3GB` (`USER_END`), MMIO 3–4GB outside user reach. RLIMIT_FRAMES counted in `addrspace_map` (no unmap API — count only grows).

## 4. Interrupt architecture

48-entry 32-bit IDT (0–31 exceptions, 32–47 IRQs), DPL0 gates. Vectors: 32 timer (PIT 100Hz), 33 keyboard, 44 mouse, 41 virtio (32+line 9, BIOS-routed, read-modify-write unmask preserving others). Level-triggered INTx acked by ISR read before EOI (no storm); shared-line strangers don't count. `irq_common` has a preemption point (need_resched → switch before iret). Handlers run IF=0 to completion, no nesting. LAPIC probed, virtual-wire mode; APIC used only for INIT/SIPI. Mouse IRQ12 never unmasked here (deaf controller → dormant). ATA IRQ14/15 deliberately masked (ADR-0063).

## 5. Scheduler architecture

Strict 32-level priorities + RR within level, behind policy ops (the RR→prio swap proved replaceability). 10-tick slices, `need_resched` consumed at IRQ return + direct `sched_yield`. States: READY/RUNNING/ZOMBIE/BLOCKED; sleep list with absolute deadlines, timer expiry (wrap-safe), `sched_wakeup` (thread+IRQ safe, priority preemption at next return). Tick pump for RX lives in `sched_tick` (100Hz, two loads idle). CPU quotas kill over-limit *user* threads at switch time. **No aging: strict priority can starve** (self-documented; ADR-0004's "fair default" unmet — §26 P1-2). THREAD_MAX 16. Boot thread prio 0 is the eternal fallback (scheduler never empty-panics in practice).

## 6. Process/thread model

Process = ownership scope: pid, CR3, thread list, 16 fds (no fork/dup/sharing), caps+uid+gid, jail root, 4 rlimits + tick accounting. Threads = 8KB PMM stacks + canary (checked every switch-in), graveyard reaping (never free under self), TSS.ESP0 reset per switch + fallback stack. Userspace: init+shell+hi+uname as flat binaries (PE→objcopy), `spawn_file` by path (spawn semantics, not image replacement). No signals, no IPC objects (ADR-0005 unimplemented; `ipc/` empty), no pipes, no sessions/jobs. Shell is single-threaded line input over blocking getchar.

## 7. Syscall ABI

35 calls, 0–34 (`SYS_MAX 34`), INT 0x80 DPL3 gate (0xEE), EAX number / EBX-EDX args / EAX result (negative `-errno`, Linux-compatible values). Table: yield/exit/print/getpid/gettid/version/getkey/meminfo/ticks, open/read/write/close/readdir/mkdir/exec/fsck, ping/dhcp/netinfo, caps/rlimit/cred/chmod/chroot, focus/winopen/winstat/wintitle/winclose, unlink/rmdir, sock, dns, winclick. Validation: ranges, wraparound, per-page present/user/writable against live tables + fuzz (T8, 20k inputs). `docs/ABI.md` generated from the table (T17 fails on skew). SDK mirrors numbers in `nova.h` (twin-update required — caught once by build break, ADR-0056). Struct-carrying calls share `nova/types.h` (drift breaks compilation). No per-call versioning, no compat table (ABI-STRATEGY §34–35 moot while 32-bit-only). **Cross-thread TOCTOU**: validate-then-bounce-copy assumes caller-stable pages, but threads share the PD: a sibling thread can rewrite a validated buffer/length mid-copy (`do_sock` SEND/RECV, `do_wintitle`, any `validate_usermem` + volatile-copy pair). No mitigation (P1-1).

## 8. VFS

One interface (`kernel/fs/fs.h`), mounts at `/` (ramfs), `/disk` (FAT32 graft), `/ext`, `/nova`. Open-file descriptions + per-process fds; 512B kernel bounce for I/O; path lookup bounded, dot-aware; jail floors and graft visibility enforced at lookup. Refcounts + `unlinked` give POSIX-style open-but-deleted. Gap writes rejected; whole-file materialize on graft open (memory cost, documented). No symlinks, no rename, no mount/unmount syscalls, no dentry cache, no page cache.

## 9. Storage

ATA LBA28 single-sector via BMIDE DMA (PIO fallback if absent), PRDs split at page/64K bounds, bounded 2M-poll waits (µs typical; covers VDI first-write stalls), `-EIO` on death. IRQs masked by design (ADR-0063: no completion signal exists; IRQ-driven DMA is a future phase, not a tweak). No NCQ/TRIM/SMART, no AHCI/NVMe/virtio-blk, no async/block layer — every FS read is a synchronous sector wait on the single boot path.

## 10. FAT32

Read/write superfloppy images (byte-deterministic `mkfat.py`): parsed BPB, chain walks with step caps, 8.3 uppercase-on-match, **no LFN**, single-entry sector cache (invalidated on write), grow-only files capped at `FS_MAX_FILE`, write-through per 512B chunk (OOM fails before disk mutation), `0xE5` delete, mkdir with dot entries. Host T10: multi-cluster bytes, offset-merge, fixture integrity + fuzz. Gaps vs fuzzing goals: no LFN plan, no concurrent-access story (single-threaded FS users today), no fsck for FAT.

## 11. EXT4

Read-only, third disk: enforced superblock/group geometry (magic, 4KB, single-group, 128B inodes), direct + single-indirect only, `rec_len` progress checks, case-sensitive exact match. No journal handling (images are clean by construction), no extents, no symlink/large-file paths, writes refused EROFS. Write support correctly not attempted.

## 12. NOVA-FS

Design + reader + journaled writer + host fsck + in-guest audit, all staged per ADR-0006/0024. 4KB blocks, single group, 128B inodes, inline extents (4) + depth-2 overflow, metadata-only ordered journal (crc32c, commit sectors, SEQ-ordered replay), redundant superblock, linear dirs, 255B opaque names, refusing posture (unknown bits, bad checksums, torn commits). Single-block dirs (ENOSPC when full), gap writes rejected. No snapshots/COW/data-checksums/multi-group — all explicitly later. `nova_fsck()` in-guest + `tools/novafsck.py` host; T13 mutates 4 ways.

## 13. PCI

Mechanism-1 config, empty-slot skip, multifunction fan-out, bridge recursion (depth ≤8), 32-entry table (drops counted aloud), known-name database, BAR parsing incl. 32-bit MMIO validation (virtio restricted to BAR2). No MSI/MSI-X, no resource allocator, no driver binding (callers walk the table directly), no hotplug, no power management.

## 14. Virtio

virtio-net only: modern transport on transitional ID, 1.0 handshake, MAC + MRG_RXBUF, 2×64 queues, 8 posted RX pages, single-descriptor TX bounce. Kernel-CR3 window for all MMIO. Interrupts routed (line 9/pin 1, vec 41), acked + counted; ring work deliberately NOT in ISR (reverted, ADR-0061). Test drill fakes a completion to prove the pump path deterministically. No virtio-blk/console/input/gpu. Ring sizing fixed; no feature renegotiation beyond the subset.

## 15. Networking

IPv4 (build/parse, no reassembly ever — fragments refused; TTL 0 refused), ICMP echo, UDP (pseudo-header checksums, RFC 768 zero-means-absent), ARP (gateway cache, broadcast requests), DHCP (DISCOVER→ACK, bounded retries, full option validation, runtime config + re-ARP), DNS A-subset (strict grammar, bounded compression jumps, NXDOMAIN→ENOENT, live-opportunistic). Loopback single-slot (overwrite semantics, documented). **No TCP, no IPv6, no firewall, no routing table** (off-subnet = EHOSTUNREACH), no socket polling/select.

## 16. Socket subsystem

8-socket pool, global port space, ephemeral pick, single-slot RX, fire-and-forget sends. Paths: loopback slot, wire TX via UDP stack, wire RX via pump+demux. Multiplexed `SYS_SOCK` (OPEN/CLOSE/SEND/RECV) with CAP_NET on all but CLOSE; static bounce buffers, length-before-touch. No sender info on delivery, no listen/accept/connect, no nonblocking flag (timeout 0 = poll), no shutdown.

## 17. Blocking/waiter architecture

Uniform pattern in two waiters (sockets, keyboard): fast path → pre-scheduler spin → check-and-park under IF=0 → single waiter + EBUSY → wake-then-clear wakers → revalidate on resume. Sockets add deadlines (EAGAIN), bind generations (EBADF on close/rebind races), and racing-delivery-wins. Keyboard has no timeout (infinite park — safe only because the audit proved the matrix). Invariants explicitly tested: early wake, timeout window, close-under-wait, second parker, ordered bytes.

## 18. Security model

Default-deny processes (nobody/nothing; init gets all/root at adoption; spawn inherits). Two caps (NET, SPAWN; DROP-only), three rlimits + tick quota (kill at switch), UID/GID + 9-bit modes with root bypass and fail-closed ordering, self-narrowing chroot (grafts invisible as ENOENT), journaled chmod (owner-or-root, NOVA-FS only). Shell can degrade its own session live. No MAC, no secure handles (plain fds/ids), no device permissions, no sandbox-escape testing beyond the matrix, no audit log.

## 19. GUI architecture

Mode-13h framebuffer (clipped primitives, DAC palette, splash), compositor (8-window pool, id=z-order, full 64KB repaint per present — no damage tracking, documented), BIOS 8x8 font with validated fallback, titles, 5 window syscalls + desktop bar, stateless widget toolkit (labels, buttons, pressed look, hit queries), shared click routing for hardware/synthetic/user clicks, 4-window demo with screenshot proof. No GPU, no acceleration, no font engine beyond BIOS glyphs, no clipboard/DnD, no multi-monitor, no window ownership (any local process arranges the shared screen — recorded future work).

## 20. Input architecture

PS/2 keyboard: Set-1 → US ASCII ring (256B), shift/backspace/enter, E0-dropped, blocking getkey with one IRQ-woken waiter, `kbd_feed` seam for deterministic tests. PS/2 mouse: full parser/cursor/click-to-focus proven via crafted bytes, but **dormant** — controller write-deaf on this VM (`MOUSE-ABSENT`, correct non-failure). No USB HID, no virtio-input, no keyboard layouts, no key-repeat in driver, no keyboard-focus protocol.

## 21. Test architecture

`tools/nova.py test`: T1–T4 static/image checks, T5 guest boot marker, T6 fault-image panic chain, T7 heap, T8 validation fuzz (20k), T9–T12 FS images + path fuzz (20k each) + corruption injection (NOVA-FS) + 4-mutant fsck (T13), T14 net checksums + 2k oracle fuzz + rogue DNS/DHCP mutants, T15 caps + monotonicity fuzz, T16 rlimit + clamp fuzz, T17 ABI freshness (generated-file skew fails), T18 dist self-verification, T19 compositor + widgets + clicks on stub VRAM, T20 mouse matrix, T21 sockets + 2k discipline fuzz. Conventions observed: hermetic, deterministic (fixed PRNG seeds — but **no checked-in corpus**, contrary to TESTING-STRATEGY), fail-loud with repro commands, pool-draining selftests, crafted-byte IRQ-path tests, fault image for panic. Missing: integration dir (empty), stress/SMP/memory-pressure suites, packet fuzz beyond crafted mutants, timing/latency assertions, coverage tracking.

## 22. Build system

Single driver: `tools/nova.py` (env/build/image/run-vbox/vbox-check/panic-test/bench-pmm/abi/dist/version/test/clean). Host-clean mingw `-m32` → PE → `ld` custom scripts → `objcopy -O binary`; C symbol `foo`→`_foo` rule (documented landmine); `-mno-sse -mno-mmx` (no XMM state saved); `-Os -Wall -Wextra`. Fixture builders in stdlib Python (byte-deterministic images). Userspace manifest builds init/hi/uname with shared libc. Gaps: no format/lint step (TESTING-STRATEGY mandates it), no dependency pinning beyond pre-existing host tools, no ccache, no parallel build, no SBOM (manifest has date/commit/ABI+hashes only).

## 23. Release process

Checklist in `docs/RELEASE.md` (6 mechanical steps) — followed for 0.1.0 and 0.2.0: VERSION single-source with banner fan-out, green-tree gate, `dist/nova-os-<ver>-<date>-<sha>/` + sha256 manifest, changelog sections, tag+push. Gaps vs a full process: no feature-freeze branch, no RC cycle, no security-audit gate, no performance-comparison gate, no signed artifacts.

## 24. ADR index

63 entries, all `accepted` except ADR-0008 (proposed/deferred — license). Eras: Phase 0 (0001–0008), Phase 1–2 (0009–0013), Phase 3–6 (0014–0018), Phase 7 (0019–0028), Phase 8 (0029–0031), Phase 9 (0032–0034), Phase 10 (0035–0038), Phase 11 (0039–0040), Phase 12 (0041–0045), Phase 13 (0046–0049), Phase 14 (0050–0056), Phase 15 (0057–0061), Phase 16 (0062–0063). Integrity notes: ADR-0002 (Rust-first) is contradicted by the tree (0 `.rs` files) with no supersession — must be resolved; ADR-0004 (fair default) vs strict-priority implementation — acknowledged in code, not in the ADR; no other silent contradictions; reverted work retained (0061). One-line decision index in Appendix A.

## 25. Known limitations (admitted)

No TCP/IPv6/firewall; EXT4 read-only; FAT32 no LFN; disk polling; mouse dormant; APs parked (no AP scheduling); no signals/IPC/pipes; no fork/dup; no ELF (flat binaries); no demand paging/page cache; compositor full-repaint; single-slot socket overwrite; DNS opportunistic; 32-bit only; no power/USB/GPU; no packages/updates; wire-to-socket has no live peer.

## 26. Technical debt (prioritized)

P0 (correctness/security/legal blockers):
- **P0-1 NO LICENSE.** No LICENSE file; ADR-0008 unratified → all-rights-reserved by default. "Open-source" must not be claimed, contributors cannot safely contribute, redistribution is legally blocked. (Non-technical, highest urgency.)
- **P0-2 ADR-0002 violated silently.** Kernel is 100% C; zero Rust files; no `unsafe`-invariant process exists because there is no Rust. Either supersede (C-first with justification + sanitizer/fuzz compensations) or start migration. A contradicted-but-open ADR rots decision authority.
- **P0-3 Cross-thread TOCTOU on syscall bounce copies.** Validate-then-copy assumes caller-stable pages, but threads share the PD: a sibling thread can rewrite a validated buffer/length mid-copy (`do_sock` SEND/RECV, `do_wintitle`, any `validate_usermem` + volatile-copy pair). No mitigation. Fix directions: copy-under-IPI-pause (heavy), fault-tolerant copyout (needs recovery design), or document + restrict (single-threaded processes only — unenforced today).
P1 (architecture/reliability):
- **P1-1 No SMP locking anywhere.** Heap/PMM/runqueue/FS/ring code all say "SMP adds it"; APs park today so this is latent, not live — but the 2-CPU VM invites false confidence. Any AP-scheduling work must start with locks + per-CPU + TLB shootdown + IPI.
- **P1-2 Strict-priority starvation.** No aging; a high-prio compute loop starves everything below (incl. shell). No benchmark quantifies it (ADR-0004 requires numbers from Phase 4 — missing).
- **P1-3 No NX/W^X/SMEP/SMAP, no ASLR.** 32-bit non-PAE makes NX impossible; user PDs clone kernel low mappings (shared frames) — user ROP reads kernel code freely. Threat-model actor 5 has a low bar past the first bug.
- **P1-4 Runqueue/sleep lists assume a single executor** (documented) — fine today, must not survive the first AP-scheduling commit.
- **P1-5 `last_used++` vs preemption.** Ring drain index advance is preemptible between load and store (documented in ADR-0061 as accepted-rare). Re-audit in v0.3; not a known bug.
P2 (performance/maintainability):
- **P2-1 11 empty scaffold dirs** (listed in Appendix C). Delete or fill; `scheduler/` vs `sched/` invites misplacement.
- **P2-2 No format/lint/CI** despite TESTING-STRATEGY mandating format+lint per PR and `ci/` existing empty.
- **P2-3 No checked-in fuzz corpus** (strategy promises `tests/` corpus; current fuzz is fixed-seed PRNG — deterministic, zero cross-run breadth).
- **P2-4 Full-repaint compositor** (64KB/present; fine at this scene size, headroom unmeasured).
- **P2-5 Tick pump at 100Hz** unmeasured under flood (claimed two loads).
- **P2-6 ATA 2M-spin worst case** (seconds on first-write stalls; acceptable, unmeasured here).
- **P2-7 SDK `SYS_*` mirror twinning** (manual; one build break already caught it — generate from the table instead).
P3/P4: UEFI, AHCI/NVMe/virtio-blk, USB, GPU, power, TCP/IPv6, signals, ELF, packages, update/rollback, multicore scheduling, NOVA-FS snapshots/COW.

## 27. Security risks (by actor)

1. Malicious app: the syscall boundary is THE barrier — validation+fuzz (T8) is the evidence; P0-3 (TOCTOU) is the hole; no NX/ASLR deepens any bug into control (P1-3).
2. Buggy driver/service: all drivers ring-0 with no isolation (ADR-0001's "isolate where practical" unfulfilled); virtio ring indices bounds-checked (good), PCI BARs validated (good).
3. Compromised service: caps/jails contain filesystem/network reach, not already-running ring-0 code; no containment for a compromised driver.
4. Malformed input: strongest area — FS images (fuzz+injection), net/DHCP/DNS (rogue mutants), syscall args (20k fuzz), boot map (range/overlap checks). Residual: shell parser unfuzzed; NOVA-FS writer covered via T12.
5. Privesc (any bug→ring0): see P1-3; `spawn` inherits everything (no privilege separation between init/shell/children except voluntary drops).
6. DMA device: unmitigated, documented (no IOMMU; BMIDE bus-masters freely). VBox-only threat today.
7. Supply chain: mingw + Python + VBox pre-existing, unpinned, no SBOM, no CI to defend; `dist/` manifest hashes outputs but not inputs.

## 28. Concurrency risks

Proven-safe: sock/kbd waiters (lost-wakeup, theft, EBUSY, close-races all gated). Single-core + IF-discipline makes runqueue/sleep-list/ring/queue manipulation sound — **conditional on no AP executor** (stated assumption; review before SMP). Residual: P1-5 (`last_used++` vs preemption), tick-pump vs thread-pump ordering (argued FIFO-safe in ADR-0061; no stress proof), heap/PMM/FS have no locking (fine single-core; SMP-blockers later), two-threads-one-stdin EBUSY yield-spin (benign, noted).

## 29. Performance bottlenecks (measured vs claimed)

Measured: PMM bitmap vs buddy (ADR-0010), heap churn ~6.4M ops/s (host), ATA bound rationale, scheduler overlap proofs (correctness, not latency). Unmeasured: syscall latency, context-switch cost, interrupt latency, tick-pump cost under flood, compositor repaint budget, DHCP/DNS wall times, boot-time breakdown, FS throughput. No profiler, no `benchmarks/` beyond PMM, no regression tracking. Biggest *known* costs: 1-sector cache with per-entry re-read on dir scans (hundreds of VM exits per walk), full-repaint presents, per-frame pump parse of broadcast noise. Biggest *unknown*: everything else.

## 30. Architecture risks

- 32-bit-only with PAE proven-impossible on the reference hypervisor → NX/SMEP/large-RAM permanently gated on long mode, which has no ADR, no branch, no prototype.
- Identity-mapped low 4GB + cloned kernel mappings: simple and fast, incompatible with KPTI-style isolation later; user/kernel share low RAM by design.
- Single runqueue + global IF-locks: correct today, pervasive assumption (every `cli` presumes no AP executor) — SMP touches nearly every subsystem.
- VFS graft model (whole-file materialize, opener-owned foreign views) fits MB-scale fixtures; no page-cache/dentry story for larger trees.
- Socket single-slot overwrite + single waiter: correct for demos, wrong shape for any real workload (needs queues + poll).
- The `nova.py` monolith (~60KB, 12 commands, single-threaded): works, but build/test/release logic in one file with no unit tests of its own.

## 31. Missing production capabilities (master-spec checklist)

Present: boot, MM basics, scheduler, user mode, VFS subset, ATA, UDP/DNS, caps/rlimits/jails, GUI basics, gates, releases. Absent: TCP/IPv6, signals, IPC, ELF, fork/exec-replace, demand paging, page cache, AHCI/NVMe/virtio-blk, USB, GPU/power, SMP scheduling, NX/ASLR, audit logging, tracing/profiling, packages/updates/rollback, CI, fuzz corpora, SBOM, secure boot, crash dumps (panic prints, nothing persists), watchdog (PIT-dead hlt-forever noted), multi-monitor, accessibility, installer, hardware matrix beyond VBox.

## 32. x86-32 readiness

Native and complete: the entire tree builds and runs `-m32`; GDT/IDT/TSS/PIC/PIT/virtio drivers are 32-bit; `arch/x86` backend selected by `__i386__`; classic 32-bit page tables; syscall INT 0x80. No 32-bit-specific debt beyond the architectural ceilings (4GB, no NX).

## 33. x86-64 readiness

Headers only: `arch/x86_64/{cpu,io}.h` are real but dormant (never selected; no `__x86_64__` build exists). Missing everything else: no long-mode entry, no 4-level paging, no GDT64/TSS64, no SYSCALL/SYSRET or compat-table design (ABI-STRATEGY calls for the `syscall` instruction + compat ADR — absent), no 64-bit IDT builder, no multiboot2, no NXE enabling, `uint32_t`-typed phys across PMM/virtio/addrspace. The interface discipline (`arch_*`, no raw asm in generic code) helps; the port is still a project, not a flag flip.

## 34. SMP readiness

Bring-up only, and good at it: MP parse (checksummed), INIT/SIPI, trampoline, AP self-check + park, `SMP-OK` gate, 8KB/AP cost documented. Everything else absent: no AP scheduling, no per-CPU data, no spinlocks, no IPI beyond boot, no TLB shootdown, no load balancing, no NUMA thought. All concurrency code assumes a single executor. Do not represent the 2-CPU VM as SMP support. First SMP commit must be locks + per-CPU + AP idle loop, behind an ADR.

## 35. Future ARM64/RISC-V readiness

Only the `arch/` split and the no-raw-asm rule point that way. Against it: `uint32_t`-typed physical addresses, x86-shaped arch APIs (cpuid/cr/msr/inb), identity-map-everywhere assumptions, BIOS boot services (E820, VGA font, INT13) with no abstraction. Verdict: portable *habits*, zero portable *code* outside the arch headers. RISC-V (no ports, no BIOS) would need a new boot + MMU backend from scratch.

## 36. Recommended v0.3 roadmap (P0 → P1 → P2 order)

- **0.3a Legal + decision hygiene (P0):** ratify ADR-0008 (Apache-2.0 recommended) and land LICENSE + headers; supersede ADR-0002 (record C-first + compensating controls, or reopen Rust properly). Without these, "open-source" and "memory-safe direction" are unclaimable.
- **0.3b Syscall hardening (P0-3 → P1):** close or bound the cross-thread TOCTOU (copy-under-IPI-pause, fault-tolerant copyout, or document+restrict); extend T8 with multi-threaded adversarial shapes; add shell-parser + packet fuzz breadth.
- **0.3c Scheduler evidence (P1-2):** measure starvation/latency/ctx-switch before any policy work (ADR-0004's missing numbers); then decide aging vs classes.
- **0.3d SMP foundations, no scheduling yet (P1-1/P1-4):** spinlocks, per-CPU data, AP idle loop, locking audit of heap/PMM/runqueue/FS; keep APs parked by default behind a flag. Re-audit P1-5 (`last_used`) in the same pass.
- **0.3e Hygiene + observability (P2):** delete or fill the 11 empty dirs; add format/lint + fill `ci/` (even local-only gates); check in fuzz corpora; generate SDK `SYS_*` from the kernel table; add boot-time breakdown + minimal `nova trace` event stream as the seed of observability tooling.
- Explicitly NOT in v0.3: TCP, ELF, AHCI/NVMe, USB, GPU, packages, secure boot, ARM64 (all P3+; each needs its own ADR with the §70 review).

---

## Appendix A — ADR index (one line each)

0001 hybrid/modular kernel (privilege only where it pays) · 0002 Rust-first (**VIOLATED — 0 .rs files**) · 0003 memory model, allocator deferred to benchmarks · 0004 scheduler policy interface + fair default (**implementation is strict-prio, acknowledged only in code**) · 0005 IPC handles+ownership (**unimplemented; ipc/ empty**) · 0006 VFS-first, FS milestone order + fuzz gates · 0007 caps/sandbox baseline · 0008 license (**NOT RATIFIED, no LICENSE file**) · 0009 custom BIOS stage1, no GRUB · 0010 bitmap allocator wins on numbers · 0011 classic 32-bit paging, PAE rejected by evidence · 0012 slab+large heap with frame tags · 0013 IDT/8259/PIT/LAPIC bring-up · 0014 threads + preemptive RR · 0015 per-process addrspaces + ring-3 entry · 0016 strict prio + SMP bring-up (APs park) + user threads · 0017 syscall ABI v1 + validation model · 0018 init+shell + getkey/meminfo/ticks · 0019 VFS+RAMFS + 6 file syscalls · 0020 exec-by-name + run · 0021 ATA PIO + FAT32-ro · 0022 FAT32 write + put · 0023 EXT4-ro · 0024 NOVA-FS design · 0025 NOVA-FS reader · 0026 NOVA-FS writer (journaled) · 0027 fsck host+guest · 0028 loader budget (768 sectors) · 0029 PCI enumeration · 0030 BMIDE DMA replaces PIO · 0031 VGA console + virtio deferral · 0032 loopback net · 0033 virtio-net wire proof · 0034 DHCP+UDP · 0035 capabilities · 0036 rlimits · 0037 permissions · 0038 jailing · 0039 SDK headers + generated ABI.md · 0040 debugging guide + dist packaging · 0041 mode-13h framebuffer · 0042 compositor · 0043 BIOS font + titles · 0044 window syscalls + bar · 0045 mouse (dormant by hardware) · 0046 unlink/rmdir + refcounts · 0047 CPU quotas · 0048 native identity · 0049 NOVA-FS chmod · 0050 sockets · 0051 DNS · 0052 virtio IRQs + PIC-cascade fix · 0053 widgets · 0054 click routing · 0055 press states · 0056 SYS_WINCLICK · 0057 sleep/wakeup · 0058 blocking recv · 0059 wire demux · 0060 RX pump + queue · 0061 tick RX + ISR-hook revert · 0062 blocking getkey + waiter discipline · 0063 disk stays polling.

## Appendix B — Gate chain (observed 2026-10-06 boot)

PMM-OK VMM-OK HEAP-OK ATA-OK PCI-OK FB-OK COMP-OK WID-OK PRESS-OK CLICK-OK MOUSE-OK MOUSE-ABSENT VIRTIO-OK NET-OK NET-WIRE-OK DHCP-OK DNS-LIVE/NOLIVE CAPS-OK RLIM-OK PERM-OK JAIL-OK SDK-OK WIN-OK TICK-OK SOCK-OK SLEEP-OK SOCKBLK-OK IRQPUMP-OK KBDBLK-OK SCHED-OK TIMER-OK USER-OK SYSCALL-OK SMP-OK INIT-OK HI-OK + COMP-FOCUS/CLICK id= markers + demo screenshot.

## Appendix C — Empty scaffolding inventory

`kernel/{core,ipc,sync,security,scheduler}` · `drivers/{display,input,net,storage}` · `fs/` · `net/` · `libs/{libc,libnova}` (real code lives in `userspace/libs/` and `sdk/`) · `tests/integration/` · `ci/` · `scripts/` · `examples/` — 11 dirs, 0 files. Recommendation: delete all in v0.3e (re-create when staffed), except keep `tests/integration/` with a README stating intent.
