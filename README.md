# NOVA OS

An operating system built from zero — bootloader to timer interrupts —
in C and x86 assembly. No Linux, no GRUB, no copy-pasted tutorials:
every byte is hand-written, measured, and tested.

> **Status: experimental (Phases 0–3 done).** It boots in VirtualBox,
> prints a clean bill of health over serial, and halts. There is no
> shell, no filesystem, no userspace yet — see [ROADMAP.md](ROADMAP.md)
> for exactly what's real and what's next. Nothing here is claimed
> done without a passing test.

```text
NOVA-OS-BOOT-MARKER-v0
boot-info ok regions=8
mem base=0x0000000000100000 len=0x000000003fef0000 type=1
...
PMM-bitmap total=261864 free=261864 largest=261864
PMM-OK
VMM-STEP-TABLES
VMM-STEP-CR
VMM-PG
VMM-CR3=0x00400000 MAP-END=0x40000000
VMM-OK
HEAP slabs=0 live=0 large=0
HEAP-OK
LAPIC base=0xfee00000 bsp=1 enabled=1
IDT-OK
TIMER-OK ticks=20
NOVA-OS-READY-HALT
```

## What's inside

| Layer | What it does |
|---|---|
| `boot/` | 512-byte BIOS stage1 (INT13 load, E820 map, A20, protected mode) + stage2 entry |
| `kernel/mm/` | Bitmap **and** buddy physical allocators (benchmarked against each other), classic paging VMM with null/stack guards, slab heap |
| `kernel/irq/` | 48-entry IDT, 8259 PIC, LAPIC probe + virtual-wire bring-up |
| `kernel/time/` | PIT 100 Hz timer, proven by observed ticks |
| `kernel/` | Serial driver, panic handler (registers + stack walk), `arch/` CPU abstraction |
| `tools/nova.py` | One driver: `env`, `build`, `image`, `run-vbox`, `test`, `bench-pmm`, `panic-test` |
| `docs/adr/` | 13 Architecture Decision Records — every big choice, with alternatives and evidence |

Highlights: the allocator was chosen by a real shootout (`bench-pmm`:
bitmap 173M vs buddy 154M ops/s, 1/40th the metadata); the PAE attempt
failed deterministically and the evidence (9-boot elimination log) is
in ADR-0011 instead of being swept under the rug.

## Run it (Windows + VirtualBox, ~5 minutes)

Prerequisites: mingw-w64 `gcc` with `-m32` support, Python 3,
[VirtualBox 7.x](https://www.virtualbox.org/). No installs performed
by the build; everything stays inside the repo.

```sh
python tools/nova.py env    # read-only host capability check
python tools/nova.py build  # compile stage1 + kernel, layout-asserted
python tools/nova.py image  # raw disk image -> VDI, attached to test VM
python tools/nova.py test   # full suite: host checks + 2 gated VM boots
```

This creates **only** an isolated `NOVA-OS-CONSTRAINED` VM
(2 CPU / 1024 MB, no network, no disk but its own) — your existing VMs
are never touched. Full walkthrough: [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md).

## Design principles

- **Verified progress over impressive demos** — see `docs/ARCHITECTURE.md`
- **Security by design** — `docs/THREAT-MODEL.md` is normative for reviews
- **Performance by measurement** — `benchmarks/`, never vibes
- **No fake progress** — `CHANGELOG.md` lists evidence per claim

## Contribute

Start with [CONTRIBUTING.md](CONTRIBUTING.md), [GOVERNANCE.md](GOVERNANCE.md),
and the `good first issue` lanes: docs, tests, build tooling, VBox
automation. Small, reviewed, tested changes only.

## License

**Not yet licensed — all rights reserved.** You may clone, read, and
learn from this code, but no reuse grant exists until ADR-0008
(`docs/adr/ADR-0008-license.md`) is ratified. Apache-2.0 is proposed.
