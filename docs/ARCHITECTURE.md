# NOVA OS — Architecture Specification (Phase 0)

Version: 0.0.0-phase0. Status: **draft, pre-implementation**.
Every claim here is design intent, not implemented behavior.

## 1. Kernel architecture: hybrid/modular

Privileged code contains only what measurably benefits from privilege:
scheduling, virtual/physical memory, interrupts, timers, CPU/SMP control,
syscalls, IPC primitives, sync primitives, core device abstraction,
security primitives, minimal VFS interface, essential early drivers, boot
init. Everything else (filesystems beyond VFS iface, network stack above
L2, graphics, audio, package management) moves toward isolated services.

Rationale and alternatives: see ADR-0001.

## 2. Portability: generic / arch / platform / driver

```text
kernel/          generic C/Rust, no inline asm, no fixed pointer width
kernel/arch/     x86, x86_64 backends behind arch_* interfaces
platform/        firmware/board specifics (BIOS/UEFI plumbing lives in boot/)
drivers/         hardware-specific, bound via stable driver iface
userspace/       architecture-independent
```

`arch_*` interface surface (initial):

```text
arch_cpu, arch_memory, arch_interrupt, arch_timer, arch_atomic,
arch_context, arch_io, arch_paging, arch_boot
```

Rules: generic code never executes privileged/arch instructions
directly; never assumes `sizeof(void*)`; all optional CPU features are
runtime-detected and gated (see §4).

## 3. Boot architecture

```text
firmware (BIOS/UEFI)
  -> bootloader (custom BIOS stage1 as of Phase 1, ADR-0009;
     Multiboot2/UEFI remain future options behind the versioned struct)
  -> boot-info structure (firmware-decoupled, versioned)
  -> arch_early -> pmm -> vmm -> interrupts -> scheduler -> drivers
  -> vfs -> init (userspace)
```

The kernel never parses firmware tables directly in generic code; the
bootloader normalizes memory map, framebuffer, modules, and command line
into one versioned struct.

## 4. CPU support (x86/x86-64 first)

x86-64 is primary; x86-32 is intentionally supported but isolated so its
constraints never degrade the 64-bit path (shared interfaces, separate
implementations). Required: protected/long mode, paging, GDT/IDT/TSS,
rings, exceptions, LAPIC + I/O APIC, timers (PIT/HPET/TSC/APIC-timer),
SMP bring-up design, CPUID feature detection with gating (NX, SMEP,
SMAP, PCID where present). Single-core boots first; SMP behind a flag.

## 5. Memory model

PMM (bitmap/buddy TBD — ADR-0003) → VMM with explicit address-space
objects → kernel heap (slab/slub-style TBD) → user mappings with
guard pages, NX, COW, mmap/mmap-file architecture. ASLR design reserved
but not implemented in Phase 2. Kernel/user split enforced by paging,
verified by tests (no RWX, no user-mapped kernel).

## 6. Process/thread/scheduler

Process = address space + handles + resources. Thread = schedulable
context. No blind `fork()` copy semantics; prefer `spawn`/`exec`.
Scheduler: preemptive, priority-aware, fair, with affinity + SMP load
balancing hooks — policy behind an interface so it stays replaceable.
Scheduler overhead and latency measured from Phase 4 (see testing).

## 7. Syscalls / IPC / VFS / net (interfaces, not implementations)

- Syscalls: versioned, namespaced (`sys_process`, `sys_memory`,
  `sys_file`, …), never exposing kernel structs; validate every
  pointer/length/handle/overflow before use. See `docs/ABI-STRATEGY.md`.
- IPC: message passing + shared memory + events + pipes/channels with
  explicit ownership/lifetime; close invalidates handles.
- VFS: single vnode/file-ops interface; RAMFS first, then FAT32.
- Net: L2→sockets layering with loopback first; crypto only from
  audited libs, never hand-rolled.

## 8. Security model

UIDs/groups/permissions + capabilities + secure handles + resource
limits + sandboxing hooks. Threat model in `docs/THREAT-MODEL.md` is
normative for every privileged interface review.

## 9. Source tree (authoritative for Phase 0)

```text
kernel/{arch/{x86,x86_64},core,mm,scheduler,process,thread,ipc,
       syscall,security,sync,time,drivers}
boot/  userspace/{init,shell,coreutils,services}  libs/{libc,libnova}
fs/ net/ drivers/{storage,net,input,display}  tools/ sdk/
tests/{unit,integration}  benchmarks/  docs/{adr}  scripts/ ci/
examples/ images/ (gitignored artifacts)
```

## 10. Build / test / release

One driver: `python tools/nova.py {env,build,test,run-vbox,image,clean}`.
Reproducible, pinned deps, checksums + SBOM at image time (design;
not yet implemented). CI gates: format, lint, compile, unit, boot test
in VirtualBox (QEMU later), fs tests, security basics. See
`docs/TESTING-STRATEGY.md`.

## 11. What is explicitly NOT decided

Bootloader protocol final choice, PMM/heap allocator family, syscall
calling convention numbers, VFS op signatures, scheduler policy
constants — all deferred to Phase 1–4 ADRs with measurements.
