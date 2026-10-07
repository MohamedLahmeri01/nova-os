# Changelog

All notable changes will be documented here. Format follows Keep a Changelog.

## [Unreleased] — v0.4 hardening (in progress)

- Legal + language hygiene: Apache-2.0 `LICENSE` (ADR-0064),
  C-first record (ADR-0065). `dist/` carries LICENSE.
- Syscall copy model: `copy_from_user`/`copy_to_user`
  centralized, all payload sites converted, T8 refusal
  vectors, TOCTOU closure with load-bearing invariant
  (ADR-0066). Fixed en route: static sock bounce race,
  whole-print cli span.
- Memory execution: read-only kernel text (caught + fixed
  AP-handoff words in .text), CPUID NX report, mapping
  isolation verified, ASLR declined with reason (ADR-0067).
- Scheduler: starvation tripwire (`STARVE-OK`), switch
  counter, workload battery (`SCHEDW-OK`), `last_used`
  audit (ADR-0068, ADR-0072). No policy change.
- SMP foundations: LOCK atomics, spinlock + irqsave pair,
  `docs/LOCKING.md`, `smp_cpu_id`, host T22 + guest
  contention proof (`SYNC-OK`); APs parked (ADR-0069).
- Hygiene: 14 empty dirs removed, `nova lint`, 3 warnings
  fixed (1 tolerated), `ci/gate.py`, wire-seed fuzz corpus
  with T14 replay (ADR-0070).
- Observability: `SYS_STATS` (35) read-only counters +
  shell `stat` + `STATS-OK` proof, `GATES-OK` summary line
  (ADR-0071). Suite 22/22.
- v0.4 security model + per-argument syscall audit
  (`docs/SECURITY-MODEL.md`, `docs/SYSCALL-ARGUMENTS.md`).
- Memory verification: runtime permission grid audit
  (`MEMMAP-OK`), user-half checks post-heap
  (ADR-0075). `KERNEL_PDS` single-sourced.
- Tracing + benchmarks: bounded event ring with panic
  dump (`TRACE-OK`), 8-workload PERF battery (`PERF-OK`)
  (ADR-0074).
- Fuzzing: FS mutant manifests (FAT32/EXT4/NOVA-FS) with
  driver + fsck replay, UDP/IP shapes + queue flood,
  shell harness T23 + corpus (strtoi overflow fixed),
  `CORPUS-OK 7/7`, fuzzing contract (ADR-0073).
  Suite 23/23 (T23 new).
- v0.5 trace fetch (Phase C, ADR-0077): `SYS_TRACEFETCH`
  (36) projected records (20B, vers/rsv/seq, FAULT->class),
  seq per emit, snapshot-under-IF + bounded copy,
  `TRACEFETCH-OK` proof, PERF trace block;
  `SYSCALL_MAX_RANGE` 4096->8192 (full fetch is 5120B);
  fixed en route: trace_read seq copy, bench eax IN+OUT
  restore (also dispatch-floor bench). Suite 23/23.
- v0.5 trace fetch verification (Phase D): adversarial
  battery `TRACEFETCH-ADV-OK` (arg classes, boundaries,
  zero-length, refetch identity, same-tick order, 600-emit
  loss proof, RO/partial mappings, IF discipline,
  fault-class matrix); fixed en route: handler blind
  sti -> save/restore. Suite 23/23.
- v0.5 timebase audit (Phase E): full consumer inventory
  (all tick-native, all wrap-safe), PIT divisor verified
  (11931 -> 100.0094 Hz), host-measured 99.37 Hz (+-2%),
  TSC evidence (Zen 3, invariant, ~3.6 GHz, 13k-cycle
  trap cost, no freq leaves); decision DEFER, PIT stays
  authoritative (ADR-0078, `docs/TIMEBASE.md`). No code
  changes. Suite 23/23.
- v0.5 runtime accounting (Phase F): per-thread
  `runtime_ticks` on the delivered-tick path (3 insns),
  exact process aggregation invariant, `SYS_RUNTIME` (37)
  read-only query + shell `rt`, adversarial ABI battery,
  `RUNTIME-OK` proof (`docs/RUNTIME-ACCOUNTING.md`).
  No scheduler policy change. Suite 23/23.
- v0.5 filesystem concurrency (Phase H): VFS big lock
  (irqsave, 12 FS syscalls), ATA blind-sti deadlock fix,
  `FSSTRESS-OK` battery (open/close, shared offsets,
  atomic writes, metadata storm, 3-graft reads, fsck),
  `docs/FILESYSTEM-CONCURRENCY.md`, ADR-0079 (supersedes
  ADR-0076 runtime contract), W8 closed. Suite 23/23.
- v0.5 network robustness (Phase I): socket-pool lock
  (irqsave, rx→sock nesting), deliver length guard,
  `NETSTRESS-OK` battery (flood/churn/late/rebind/pair),
  +7 corpus seeds, 2000-frame demux flood,
  `docs/NETWORK-ROBUSTNESS.md`, ADR-0080. Suite 23/23.
- v0.5 shell robustness (Phase J): ping octet saturation
  (signed-overflow UB removed), dispatch-confusion +
  arg-count + numeric batteries, full-range-byte fuzz,
  state-reset content proofs, +4 corpus seeds,
  `docs/SHELL-ROBUSTNESS.md`. No grammar/features added.
  Suite 23/23.
- v0.5 scheduler observability (Phase G): state-model +
  measurement audit (`docs/SCHEDULER-OBSERVABILITY.md`);
  added wake-resume proof + 200-query cost bound
  (`query200 ticks=0`) to the runtime battery; shell `rt`
  prints `runtime_ticks=` explicitly. No policy, slice,
  timer, trace, or ABI changes. Suite 23/23.

## 0.2.0 - 2026-10-06 (second release)

Input, blocking I/O, and a living network stack: user-space
UDP sockets + DNS (14a-b), widgets + clicks (14d-e),
sleep/wakeup + blocking sockets + wire RX + tick pump
(15-i..v), blocking keyboard (16-i). Suite 21/21 on the
constrained VM. ABI v1 grows one call (SYS_WINCLICK 34);
GETKEY now parks instead of polling (documented).
Known limitations (carried from 0.1.0, still true): mouse
motion needs a talking 8042 (dormant here), no IRQ-driven
virtio drain (built, reverted: nesting hazard, see
ADR-0061), secure boot and IOMMU deferred; wire-to-socket
delivery has no live peer traffic yet (pump + demux proven,
peer pending).

### Added

- Phase 14a sockets (tested 2026-10-06, `nova test` 21/21):
  loopback-first UDP sockets, multiplexed SYS_SOCK, shell
  uloop, SOCK-OK gate, T21 discipline fuzz (ADR-0050).
- Phase 14b DNS (tested 2026-10-06, `nova test` 21/21):
  A-record resolver, codec vectors, opportunistic live
  query, SYS_DNS, shell nslookup (ADR-0051).
- Phase 14c virtio IRQs (tested 2026-10-06, `nova test` 21/21):
  PCI routing, ISR field + count, post-traffic assert;
  fixed slave-PIC cascade masking (ADR-0052).
- Phase 14d widget toolkit: `wid_label`/`wid_button` over
  `comp_geom`, guest `wid_selftest` (`WID-OK` gate), teal
  about dialog in compositor demo, T19 coverage (ADR-0053).
  Suite 21/21 on the constrained VM.
- Phase 14e-i click routing: shared `comp_click` (hit +
  focus + repaint) for mouse press edges and synthesized
  clicks, `CLICK-OK` gate, demo clicks the dialog's OK
  button every boot (ADR-0054). Suite 21/21.
- Phase 14e-ii press states: `wid_hit` edge-matched to
  `comp_hit`, `wid_button_down` pressed look over a shared
  painter, `PRESS-OK` gate, demo renders the OK button down
  on the click frame (ADR-0055). Suite 21/21.
- Phase 14e-iii user-space click: `SYS_WINCLICK` 34 +
  `nova_winclick` + shell `click X Y`, dispatch vectors in
  `win_selftest` (ADR-0056). Suite 21/21.
- Phase 15-i sleep/wakeup: `THREAD_BLOCKED`, timer-driven
  expiry, `sched_sleep`/`sched_sleep_until`/`sched_wakeup`,
  early-wake + timeout proof (`SLEEP-OK` gate, no callers
  yet) (ADR-0057). Suite 21/21.
- Phase 15-ii blocking `sock_recv`: fast/poll/spin/park
  tiers, per-socket waiter + bind generation, guest-only
  threaded proof (`SOCKBLK-OK` gate: wake-with-data, expiry,
  close-under-waiter EBADF) (ADR-0058). Suite 21/21.
- Phase 15-iii wire demux: `sock_demux_frame` port-matches
  Ethernet frames into socket slots over the shared wake
  path, proven by a host T21 frame matrix (T21 now links
  the real net.c); unwired, so no guest gate yet (ADR-0059).
  Suite 21/21.
- Phase 15-iv single-ingress RX: `net_rx_pump` drains the
  ring into socket slots or a pending queue, `rx_wait`
  reworked onto the queue (all legacy flows drive the pump;
  DHCP proves the guest path), T21 pump vectors (ADR-0060).
  Suite 21/21.
- Phase 15-v tick-driven RX: ring pump in `sched_tick`
  (wire-to-waiter wake within a tick, IF-saving subsystem
  lock, ring drill with `IRQPUMP-OK` gate; a device-ISR
  pump hook was built then reverted for nesting hazards)
  (ADR-0061). Suite 21/21.
- Phase 16-i blocking `getkey`: parked readers woken by the
  IRQ push (`KBDBLK-OK` gate), lost-wakeup/wake-theft
  discipline fixed in both keyboard and socket waiters
  (ADR-0062). Suite 21/21.
## 0.1.0 - 2026-10-05 (first release)

All Phase 0-13d work, suite 20/20 on the constrained VM.
Versioned binaries + manifest via `nova dist`; ABI v1;
see docs/RELEASE.md for scope and limitations.


## [Unreleased] — 0.0.0-phase0

### Added

- Phase 1 boot chain (tested 2026-09-29, `nova test` 5/5):
  custom BIOS stage1 (`boot/stage1.S`, 488B), stage2 trampoline,
  32-bit kernel (`kernel/pm_main.c`) printing boot marker + E820 map
  over COM1, image builder (raw → VDI), serial-verified `run-vbox`
- `kernel/arch/{x86,x86_64}/io.h` port-I/O abstraction + `kernel/serial.c`
- `docs/TOOLCHAIN.md` (host-clean PE→binary pipeline), ADR-0009
  (bootloader decision), `docs/RESOURCE-BUDGET.md` (2 CPU / 1 GB),
  isolated `NOVA-OS-CONSTRAINED` test VM recipe
- Panic handler (`kernel/panic.{h,c}`: GP regs incl. CR0/2/3, guarded
  EBP walk, `nova panic-test` fault image, T6 gate — 6/6 green).
  Fixed a real reversed pusha-index bug found by cross-checking the
  disassembly against the serial output.
- Phase 2a PMM (tested 2026-09-29, `nova test` 6/6 with PMM-OK gates):
  `kernel/mm/` front + bitmap/buddy backends behind one interface,
  `benchmarks/pmm/` shootout harness + `nova bench-pmm`, ADR-0010
  (bitmap wins: tied throughput, 2.3x free speed, 1/40th metadata).
  Corrected the harness arena to the exact VBox map (261872 frames)
  after the guest reported total=261864.
- Phase 2b VMM (tested 2026-09-29, `nova test` 6/6 with VMM-OK gates):
  classic 32-bit PAE-less identity paging (null + stack guards,
  kernel/user split reserved, `kernel/mm/vmm.c` on `arch_cpu`
  CR/MSR/CPUID backends, `.text`/`.datax` W^X-ready link split with
  NOLOAD bss zeroed at entry, `boot/mem_layout.h` for shared
  constants).   A full PAE+NX implementation triple-faulted
  deterministically under VBox NEM; 9-boot elimination + ADR-0011
  record the evidence, NX is honestly deferred to long mode.
- Phase 2c heap (tested 2026-09-29, `nova test` 7/7 with HEAP-OK + T7):
  slab caches + large runs over tagged frames (`kernel/mm/heap.c`),
  PMM contiguous API, host test (`tests/unit/test_heap.c`, 50k churn
  at ~6M ops/s, exact restore), ADR-0012. Fixed: slab slot alignment,
  trim/spare accounting, host pointer truncation, `pmm_init` metadata
  placement abstraction.
- Phase 3 interrupts+timer (tested 2026-09-29, `nova test` 7/7):
  48-entry IDT with macro stubs (`kernel/irq/idt.S`), PIC remap,
  PIT 100Hz with observed ticks, LAPIC probe + virtual-wire bring-up
  via `vmm_map_mmio`, ud2-proven trap chain (ADR-0013). Fixed: PIC
  mask 0xFD→0xFE (masked timer!), post-PG invalidation (invlpg + CR3
  reload), C frame call-push accounting.
- Phase 4a threads+scheduler (tested 2026-10-01, `nova test` 7/7):
  kernel threads, context switch, minimal processes, preemptive
  round-robin behind policy ops, tick-overlap preemption proof
  (ADR-0014). Fixed: test design that serialized workers behind full
  slices, shared   runqueue/process link field, cli guards on pick.
- Phase 4b address spaces + user mode (tested 2026-10-01, `nova test`
  7/7): per-process PDs (`kernel/mm/addrspace.c`), GDT/TSS
  (`kernel/gdt.c`), ring-3 probe with CS proof + INT 0x81 return
  (`kernel/user.S`, `user.c`, setjmp/longjmp), `USER-OK` (ADR-0015).
  Fixed: user selectors reload (DPL0 fault), physical-vs-virtual
  stack address confusion.
- Phase 4c priority+SMP+user-threads (tested 2026-10-01, `nova test`
  7/7): strict 32-level scheduler, MP-table SMP bring-up with parked
  APs, scheduled user threads via uniform trampoline entry (ADR-0016).
  Fixed: mingw underscore rule (`_foo` in C is `__foo`), relocated
  relative calls, MP flags byte, strict-priority observer starvation,
  IF restore on longjmp path, fill-before-copy trampoline data.
- Phase 5 syscalls (tested 2026-10-01, `nova test` 8/8): INT 0x80 ABI
  v1 with table dispatch (yield/exit/print/getpid/gettid/version),
  range + per-page validation, host fuzz T8, guest negative test
  (ADR-0017). Fixed: syscall frame struct (arg points past call
  machinery), gate readback byte, fresh-frame register ordering.
- Phase 6 userspace (tested 2026-10-02, `nova test` 8/8): init+shell
  static binary (own process/PD, `INIT-OK`), builtins help/echo/mem/
  ticks/clear/exit, syscalls getkey/meminfo/ticks, PS/2 IRQ1 driver,
  interactive proof over VBox scancodes with real PMM/tick values
  (ADR-0018). Fixed: blob-symbol underscore rule, validation walking
  the current CR3, user data segments in the entry trampoline.
- Phase 7a VFS+RAMFS (tested 2026-10-03, `nova test` 9/9): ramfs at
  `/` with seed content, syscalls 9-14 (open/read/write/close/
  readdir/mkdir) on per-process fd tables, shell ls/cat/mkdir,
  interactive proof over VBox scancodes, host T9 (20k-path fuzz)
  (ADR-0019). Fixed: seed-length expectation, __chkstk_ms via small
  kernel frames, scancode-letter discipline in proofs.
- Phase 7b exec (tested 2026-10-03, `nova test` 9/9): generalized
  spawn (`spawn_image`/`spawn_file`), second user program `/bin/hi`
  seeded at boot, `SYS_EXEC` + shell `run`, boot `HI-OK pid=5` and
  interactive `child pid=6` proofs (ADR-0020). Fixed: HI-OK excluded
  from fault-image gates, shell handler naming.
- Phase 7c ATA+FAT32 (tested 2026-10-03, `nova test` 10/10): ATA PIO
  driver with LBA0 selftest, deterministic FAT32 data disk on IDE
  P0D1, read-only FAT32 grafted at /disk, shell `ls /disk` +
  `cat /disk/hello.txt` proofs, host T10 (20k-path fuzz) (ADR-0021).
  Fixed: IDT loaded before slow polling (timer-tick triple fault),
  fast bounded ATA spins, ENOTDIR propagation, BPB offset 32.
- Phase 7d FAT32 write (tested 2026-10-03, `nova test` 10/10): ATA
  sector writes, FAT allocator + create/write-at/delete/mkdir,
  write-through syscalls with disk O_CREAT, shell `put`, interactive
  `put`/`cat` proof, hermetic guest + host byte-exact tests
  (ADR-0022). Fixed: empty-file materialize, fresh-VDI write stalls,
  ENOTDIR propagation, alloc-rollback counting.
- Phase 7e EXT4 read (tested 2026-10-03, `nova test` 11/11): ATA
  secondary channel, deterministic EXT4 data disk on IDE P1D0,
  read-only EXT4 grafted at /ext, shell `ls /ext` +
  `cat /ext/HELLO.TXT` proofs (lowercase correctly ENOENT),
  host T11 (20k-name fuzz) (ADR-0023). Fixed: leading-slash skip in
  split_parent, 15-vs-16   byte count, Shift scancodes for uppercase.
- Phase 7f-1 NOVA-FS reader (tested 2026-10-03, `nova test` 12/12):
  deterministic fixture on IDE P1D1, read-only driver (SB fallback,
  inode crc, inline extents) grafted at /nova, shell `ls /nova` +
  `cat` proofs, host T12 with corruption-injection (ADR-0025).
  Fixed:   SB layout pointers, stale-image refusal, edit discipline.
- Phase 7f-2 NOVA-FS writer (tested 2026-10-03, `nova test` 12/12):
  ordered metadata journal (seq-ordered replay), allocator,
  create/write/delete/mkdir, write-through syscalls, shell `put`,
  idempotent guest tests, power-cut + cross-boot durability proofs
  (ADR-0026). Fixed: replay ordering, slot splitting, slack-loop
  underflow, per-call block budget.
- Phase 7f-3 fsck (tested 2026-10-03, `nova test` 13/13): host
  `novafsck.py` plus in-guest `nova_fsck()` behind `SYS_FSCK` +
  shell `fsck`, self-audit gate, T13 (pristine + 4 mutation classes)
  (ADR-0027). Fixed: global `-mno-sse -mno-mmx` (SSE #UD with no
  XMM state), verified zero SIMD in both binaries.
- Phase 8a loader budget (tested 2026-10-03, `nova test` 13/13):
  stage1 chunked INT13 loop (496/510B), cap 127 -> 768 sectors,
  padded 140-sector   boot proof (ADR-0028).
- Phase 8b PCI enumeration (tested 2026-10-03, `nova test` 13/13):
  mechanism-1 scan with bridge recursion, VBox device table,
  `PCI-OK` gate with host-bridge + IDE asserts (ADR-0029). Fixed:
  PIIX4 IDE ID, VBox VGA/guest names, vendor-word slot skip.
- Phase 8c BMIDE DMA (tested 2026-10-03, `nova test` 13/13):
  bus-master disk I/O (PRD, MWDMA2, polled completion) with
  DMA-vs-PIO proofs and global routing, `DMA-OK` gate (ADR-0030).
  Fixed: direction-bit polarity (VBox asserts it), CLS/latency
  programming, serial.log hygiene per manual boot.
- Phase 8d VGA console (tested 2026-10-03, `nova test` 13/13):
  80x25 text mirror at the serial choke point, cell + cursor
  selftest, `VGA-OK` gate, screenshot proof (ADR-0031). Deferred:
  virtio (no attachable device; net recipe decision -> Phase 9).
- Phase 9a loopback net (tested 2026-10-03, `nova test` 14/14):
  IPv4 + ICMP echo over loopback, `SYS_PING` + shell `ping`,
  `NET-OK` gate, T14 oracle fuzz (ADR-0032). Fixed: odd-vector
  oracle, octet parser, request/reply test discipline.
- Phase 9b virtio-net wire proof (tested 2026-10-04,
  `nova test` 14/14): modern-only virtio-net driver (MAC +
  MRG_RXBUF, 12B headers, 2x64 queues), ARP + gateway ping over
  NAT + virtio, `VIRTIO-OK` + `NET-WIRE-OK` gates, pcap proof
  (ADR-0033). Fixed: QueueSize write-back, desc layout, 12B
  header negotiation, stale-binary/timestamp discipline.
- Phase 9c DHCP + UDP (tested 2026-10-04, `nova test` 14/14):
  UDP datagrams + DHCP client (dynamic self/gw/mask, rapid-commit
  ACK accepted), frag/TTL refusal, `SYS_DHCP` + `SYS_NETINFO`,
  shell `ip`/`dhcp`, `DHCP-OK` gate, pcap proof (ADR-0034).
  Fixed: decimal magic cookie 99.130.83.99, self-consistent
  build+test vectors.
- Phase 10a capabilities (tested 2026-10-04, `nova test` 15/15):
  per-process caps + uid, CAP_NET/CAP_SPAWN gates, monotonic
  SYS_CAPS, spawn inheritance, shell caps/dropcap, CAPS-OK gate,
  T15 monotonicity fuzz (ADR-0035). Fixed: NULL-safe sched_enter
  (found via objdump on the faulting eip).
- Phase 10b resource limits (tested 2026-10-04, `nova test` 16/16):
  per-process fd/file-size/frame ceilings, lower-only SYS_RLIMIT,
  shell rlimit, RLIM-OK gate, T16 contract fuzz (ADR-0036).
  Fixed: stack-process fixture hygiene for new process fields;
  addrspace_map grown an int return for ceiling refusal.
- Phase 10c permissions (tested 2026-10-04, `nova test` 16/16):
  node uid/gid/mode, triplets + root bypass, SYS_CRED +
  SYS_CHMOD, shell id/setuid/chmod, PERM-OK gate, T9 triplet
  vectors (ADR-0037).
- Phase 10d jailing (tested 2026-10-04, `nova test` 16/16):
  per-process jail roots, confined lookup with floor clamp,
  graft invisibility, narrowing-only SYS_CHROOT, spawn
  inheritance, shell chroot, JAIL-OK gate, T9 walk vectors
  (ADR-0038). Phase 10 complete.
- Phase 11a SDK headers (tested 2026-10-05, `nova test` 17/17):
  sdk/include + user.ld, program manifest, /bin/uname
  sample, generated docs/ABI.md + T17 freshness, SDK-OK gate
  (ADR-0039).
- Phase 11b debugging + dist (tested 2026-10-05, `nova test` 18/18):
  docs/DEBUGGING.md recipe ladder + pattern catalog, `nova dist`
  versioned packaging with sha256 manifest, T18 verification
  (ADR-0040). Phase 11 complete.
- Phase 12a framebuffer (tested 2026-10-05, `nova test` 18/18):
  VGA mode 13h from stage1, fb driver + DAC palette, splash,
  FB-OK gate, screenshot proof; VGA text retired from boot
  (ADR-0041).
- Phase 12b compositor (tested 2026-10-05, `nova test` 19/19):
  window pool + z-order layers, focus highlight + cycle,
  demo scene with markers, COMP-OK gate, T19 pixel test
  (ADR-0042).
- Phase 12c text + titles (tested 2026-10-05, `nova test` 19/19):
  BIOS font with fallback, fb_text, window titles, console
  demo, title-ink proofs, T19 synthetic pixels (ADR-0043).
- Phase 12d window system (tested 2026-10-05, `nova test` 19/19):
  5 window syscalls + shared types.h, menu bar, shell window
  commands, WIN-OK gate (ADR-0044). Phase 12 complete.
- Phase 12e mouse (tested 2026-10-05, `nova test` 20/20):
  PS/2 parser + cursor + click-to-focus, dormant design
  for deaf controllers, MOUSE-OK gate, T20 wiring test
  (ADR-0045).
- Phase 13a lifecycle (tested 2026-10-05, `nova test` 20/20):
  unlink/rmdir syscalls, node refcounts, graft deletes,
  shell rm/rmdir, lifecycle proofs (ADR-0046).
- Phase 13b CPU quotas (tested 2026-10-05, `nova test` 20/20):
  tick accounting, preemption kill, RLIMIT_TICKS + shell,
  TICK-OK gate (ADR-0047).
- Phase 13c native identity (tested 2026-10-05, `nova test` 20/20):
  ext4/nova uid/gid/mode passthrough, fixture identities,
  create-time stamping (ADR-0048).
- Phase 13d on-disk chmod (tested 2026-10-05, `nova test` 20/20):
  journaled NOVA-FS mode writes, fs_chmod routing, round-trip
  proofs (ADR-0049).















- Phase 7f NOVA-FS design (2026-10-03, doc-only): `docs/NOVA-FS.md`
  v0.1 (extent layout, ordered metadata journal, refusal rules,
  staged 7f-1..3 plan) adopted in ADR-0024; lesson table from the
  three implemented filesystems; arithmetic verified by execution.
- Architecture specification (`docs/ARCHITECTURE.md`)
- ADRs 0001–0008 (kernel arch, language, memory, scheduler, IPC,
  filesystem, security, license evaluation)
- Threat model, ABI strategy, testing strategy, environment report
- `tools/nova.py` build/test stub (VBox-first, read-only env check)
- Minimal freestanding boot stub (prototype, untested)

### Status

Experimental. Not bootable. Not secure. Not performant. Not released.
