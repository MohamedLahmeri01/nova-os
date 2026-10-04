# ADR-0029: PCI enumeration (mechanism 1, bridge recursion)

Status: accepted (Phase 8b). Date: 2026-10-03.

## Context

Phase 8 targets guest hardware (virtio next), which starts with
finding devices: mechanism-1 config access, full bus/slot/function
scan, bridge recursion, and a device table for our VBox hardware.
No BAR sizing yet (virtio needs addresses, not sizes; BIOS-assigned
BARs read as-is).

## Decision

`kernel/drivers/pci.c`: 0xCF8/0xCFC dword access, empty-slot skip
on the vendor word, multifunction fan-out on header-type bit 7,
PCI-PCI bridge recursion (secondary bus, depth ≤ 8), 32-entry table
(dropped counted, never silent). Known names for 440FX, PIIX3/4,
VBox VGA/guest, E1000, virtio range. Selftest asserts the 440FX
host bridge + an IDE function (7010 or 7111: PIIX3 vs PIIX4
chipsets) and zero drops; prints one line per device + `PCI-OK`
(gated in run-vbox + panic markers).

## Evidence

Guest log: 6 devices (440FX, PIIX3-ISA both funcs incl. PIIX4-IDE
7111, VBox-VGA, VBox-GUEST, PIIX4-PM) + `PCI-OK`; `nova test` 13/13.

## Hard lessons

- VirtualBox presents PIIX4 IDE (8086:7111), not PIIX3 (7010):
  asserting one ID panics on real hardware variety; accept the set.
- VBox vendor IDs are mnemonic-swapped from guesses (BEEF = VGA
  display class 03, CAFE = base peripheral 08): trust the class
  bytes, not the cute hex.
- Empty-slot skip must read offset 0 (vendor); offset 12 works by
  accident (all-F) until a device with rev/prog-if 0xFFFF exists.

## Consequences

- No BAR sizing, no driver binding, no MSI: virtio phase (8c) adds
  BAR mapping + a minimal virtio-net or block driver against this
  access layer.
- Kernel at 127 load sectors (cap now 768): growth room restored.
