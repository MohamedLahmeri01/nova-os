# Changelog

All notable changes will be documented here. Format follows Keep a Changelog.
No releases yet.

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
  Fixed: SB layout pointers, stale-image refusal, edit discipline.
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
