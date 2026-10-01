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

## Phase 4 — Processes / Threads / Scheduler (IN-PROGRESS, 4a done)

- [x] Threads (8KB canaried stacks, trampoline, exit/graveyard),
  minimal process containers, preemptive round-robin behind policy
  ops, tick-window overlap proof (`SCHED-OK`, ADR-0014)
- [x] T5/T6 gates strengthened (SCHED-OK); 7/7 green
- [ ] Fairness/priority, SMP bring-up + balancing, per-process address
  spaces, user mode (Phase 4b)

## Phase 5 — Syscalls

Versioned syscall ABI, pointer/length validation, fuzz corpus from day one.

## Phase 6 — Userspace

kernel → init → shell over serial/framebuffer console.

## Phase 7 — Filesystem

VFS → RAMFS → FAT32 → EXT4(read) → NOVA-FS design.

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
