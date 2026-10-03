# NOVA OS Roadmap

Status convention: `done / in-progress / prototype / planned / blocked`.
No phase is marked done without tests + evidence.

## Phase 0 — Architecture (DONE)

- [x] Environment inspection (`docs/ENVIRONMENT.md`)
- [x] Repository skeleton
- [x] Architecture spec (`docs/ARCHITECTURE.md`)
- [x] ADRs 0001–0008
- [x] Threat model, ABI strategy, testing strategy
- [x] Build stub (`tools/nova.py`) validated on host
- [x] VirtualBox test-VM recipe validated (isolated, no existing VMs touched)
- [x] Constrained budget locked: 2 CPU / 1024 MB (`docs/RESOURCE-BUDGET.md`)

## Phase 1 — Boot (IN-PROGRESS)

Goal (adjusted): custom BIOS stage1 → 32-bit protected-mode kernel →
E820 memory map → COM1 serial → halt, in `NOVA-OS-CONSTRAINED`
(2 CPU / 1 GB) under VirtualBox. x86-64 long mode deferred with reasons
(see ADR-0009); ISO format deferred (raw → VDI, no xorriso on host).

- [x] Bootloader decision recorded (ADR-0009: custom stage1, no GRUB)
- [x] Serial (COM1) logging (`kernel/serial.c` on `arch_io` abstraction)
- [x] Boot-info structure decoupled from firmware (`boot/boot_info.h`)
- [x] `nova run-vbox` boots VDI in isolated test VM, serial to file
- [x] Integration test T5: boot + serial marker, 5/5 green 2026-09-29
- [x] Panic handler with registers/stack trace (`kernel/panic.c`, test T6,
  6/6 green 2026-09-29; numeric EIPs only, symbol resolution deferred)
- [x] Serial boot log shows clean E820 map (see `build/serial.log`)

## Phase 2 — Memory (DONE: allocator, paging, heap)

- [x] Allocator shootout: bitmap vs buddy, measured at 1GB
  (`nova bench-pmm`, ADR-0010: bitmap wins Phase 2a on footprint/speed)
- [x] PMM in guest: arena from E820, carved metadata, self-test `PMM-OK`
  (T5/T6 gates, 6/6 green 2026-09-29) + contiguous runs for the heap
- [x] Paging/VMM: classic 32-bit identity map, null + stack guards,
  kernel/user split reserved, `VMM-OK` (6/6 green 2026-09-29).
  PAE attempt failed deterministically under VBox NEM (9-boot
  elimination); non-PAE shipped per ADR-0011, NX deferred to long mode
- [x] Kernel heap: slabs + large runs, tagged frames, host test T7,
  `HEAP-OK` (7/7 green 2026-09-29, ADR-0012)

## Phase 3 — Interrupts + Timer (DONE: IDT/PIC/PIT/LAPIC-probe)

- [x] 48-entry IDT (exceptions + IRQs), fatal traps with vector/code/EIP
  (+CR2 on PF), proven by ud2 fault image (`TRAP vec=6` → `trap-UD`)
- [x] 8259 remapped, IRQ0-only, spurious EOI; PIT 100Hz with observed
  ticks (`TIMER-OK`); LAPIC probed + virtual-wire mode (ADR-0013)
- [x] T5/T6 gates strengthened (IDT-OK, TIMER-OK, trap chain); 7/7 green
- [ ] HPET/TSC calibration, IOAPIC routing, SMP bring-up → Phase 4+

## Phase 4 — Processes / Threads / Scheduler (DONE: 4a+4b+4c)

- [x] Threads (8KB canaried stacks, trampoline, exit/graveyard),
  minimal process containers, preemptive round-robin behind policy
  ops, tick-window overlap proof (`SCHED-OK`, ADR-0014)
- [x] Per-process address spaces (kernel clone + private user half)
  and ring-3 round-trip with CS proof (`USER-OK`, ADR-0015)
- [x] Strict priorities + RR (proof), SMP bring-up (MP parse, AP park,
  `SMP-OK`), scheduled user threads with overlap proof
  (`USER-SCHED-OK`), TSS.ESP0 invariant enforced (ADR-0016)
- [x] T5/T6 gates strengthened; 7/7 green
- [ ] Fairness/aging, AP scheduling + balancing, user yield (Phase 4d+)

## Phase 5 — Syscalls (DONE: ABI v1 + validation + fuzz)

- [x] INT 0x80 gate (DPL3), versioned table dispatch: yield, exit,
  print, getpid, gettid, version (`SYSCALL-OK`, ADR-0017)
- [x] Range + per-page validation (shared guest/host logic), errno
  returns, host fuzz T8 (edge corpus + 20k randomized, oracle-checked)
- [x] Guest negative test (bad pointer → -EFAULT, not a fault)
- [x] T5/T6 gates strengthened; 8/8 green
- [ ] Handles, blocking calls, full MMU-differential fuzz (Phase 5+)

## Phase 6 — Userspace (DONE: init + interactive shell)

- [x] init+shell static binary (`userspace/init`, `init.ld` base
  0x80000000, 4140B) embedded as `init_blob.o`, own process/PD/code/
  stack via `spawn_init()` (`INIT-OK`, ADR-0018)
- [x] Shell builtins: help, echo, mem, ticks, clear, exit; new
  syscalls getkey/meminfo/ticks; PS/2 IRQ1 driver (Set-1 ring)
- [x] Interactive proof: typed help/mem/ticks over VBox scancodes ->
  real PMM/tick values; `nova test` 8/8 green
- [ ] Blocking input, job control, exec-by-name (arrives with FS)

## Phase 7 — Filesystem (7a DONE: VFS + RAMFS)

- [x] ramfs at `/` (`kernel/fs/ramfs.c`): dot-aware lookup, 64KB
  doubling files, seed /hello.txt + /docs/readme.txt (`FS-OK`,
  ADR-0019)
- [x] Syscalls 9-14 (open/read/write/close/readdir/mkdir) on
  per-process fd tables of 16; shell ls/cat/mkdir; interactive proof
  over VBox scancodes; `nova test` 9/9 with host T9 (20k-path fuzz)
- [ ] 7b: exec-by-name (`run`), FAT32 read, EXT4(read), NOVA-FS design
  - [x] 7b-exec DONE: `spawn_image`/`spawn_file`, second user program
    `/bin/hi` (multi-binary build), `SYS_EXEC` + shell `run`,
    boot `HI-OK` proof + interactive `child pid=N` proof, 9/9
    (ADR-0020)
  - [ ] FAT32 read, EXT4(read), NOVA-FS design
  - [x] 7c-FAT DONE: ATA PIO driver (LBA28 polling, IRQ14/15 masked),
    deterministic mkfat.py image on IDE P0D1, read-only FAT32 (parsed
    BPB, chains, 8.3, sector cache) grafted at /disk, shell reads it
    unchanged, 10/10 with host T9+T10 (ADR-0021)
  - [ ] FAT32 write, EXT4(read), NOVA-FS design

## Phase 8 — Drivers

PCI → virtio → storage/input/display (VBox guest hardware first:
PIIX3/ICH9, VGA/VMSVGA, i8042/PS2, Intel PRO/1000).

## Phase 9 — Networking

Ethernet → ARP → IPv4 → ICMP → UDP → TCP → sockets (loopback first).

## Phase 10 — Security hardening

Capabilities, sandboxing, resource limits, secure handles.

## Phase 11 — SDK

Headers, cross-toolchain recipe, debugging guide, packaging stub.

## Phase 12 — Desktop (explicitly last)

Framebuffer → compositor → window system → toolkit → desktop.

## Non-goals for 0.x

ARM64/RISC-V ports (isolation only), full POSIX, SMP scheduler
optimality, NOVA-FS snapshots, audio server, package signatures infra.
