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
- Architecture specification (`docs/ARCHITECTURE.md`)
- ADRs 0001–0008 (kernel arch, language, memory, scheduler, IPC,
  filesystem, security, license evaluation)
- Threat model, ABI strategy, testing strategy, environment report
- `tools/nova.py` build/test stub (VBox-first, read-only env check)
- Minimal freestanding boot stub (prototype, untested)

### Status

Experimental. Not bootable. Not secure. Not performant. Not released.
