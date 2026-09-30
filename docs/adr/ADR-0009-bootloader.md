# ADR-0009: Bootloader — minimal custom BIOS stage1 (no GRUB dependency)

Status: accepted (Phase 1). Date: 2026-09-29.

## Context

Phase 1 needs bootloader → kernel → serial → panic on VirtualBox with a
2 CPU / 1 GB budget, using only tools already on the host (mingw gcc,
Python, VBoxManage). Missing: nasm, grub-mkrescue, xorriso, QEMU — and
project policy forbids silent host installs.

## Decision

Custom 512-byte BIOS stage1 (`boot/stage1.S`, GAS `.code16`): INT13 LBA
load of stage2 to 0x10000, E820 collection with 24-byte stride matching
`nova_mem_region_t` (verified: firmware here writes 24-byte entries;
a 20-byte-stride prototype produced a corrupted map — fixed with
evidence, see serial logs), A20 via BIOS + port 0x92 with alias
verification, flat GDT, 32-bit protected-mode entry. Image assembly in
pure Python (NVSC sector-count patch, AA55 signature); raw → VDI via
VBoxManage; serial marker verified by `nova run-vbox` / test T5.

## Alternatives considered

- GRUB + Multiboot2 (rejected for now: needs grub-mkrescue/xorriso
  installs; revisit when the host policy allows or CI needs QEMU).
- UEFI (rejected for now: FAT image crafting + PE bootloader is Phase 2+
  complexity; BIOS path proves the pipeline first).
- ELF cross-toolchain (rejected for now: PE link + objcopy -O binary
  works; revisit via ADR when 64-bit long mode lands).

## Consequences

- Stage1 must stay ≤ 510 bytes (build asserts); every byte has a reason.
- Long mode, Multiboot2 compat, and UEFI remain explicit future work;
  the versioned `nova_boot_info` struct insulates generic code from
  whichever loader comes next.
