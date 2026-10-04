# ADR-0031: VGA text console (serial mirror) + virtio deferral

Status: accepted (Phase 8d). Date: 2026-10-03.

## Context

Phase 8 listed virtio next, but no virtio device exists on the VM:
VirtIO-SCSI controller creation fails even on a fresh VM
(E_INVALIDARG — environment limitation, not our config), and the
only attachable virtio device is a NIC, which needs both a NIC
recipe change (isolation: NIC none) and a network stack (ARP/DHCP/
UDP/IP) to prove anything. Shipping an undrivable virtio transport
would be theater. The honest display milestone (VGA already
enumerated at 00:02.0) unblocks all future interactive work instead.

## Decision

`kernel/drivers/vga.c`: 80x25 text at 0xB8000, software row/col
with hardware cursor follow, scroll by row copy, `\\r \\n \\b`
handling (+ tab stops). `serial_putc` mirrors every char to VGA
(one call site: the whole boot log + shell appear on screen).
`vga_selftest` (corner cell write/readback + cursor round-trip)
gates `VGA-OK`. Screenshot proof via VBox screenshotpng.

## Evidence

Screenshot shows the live boot log through `> HI-OK pid=5`;
`nova test` 13/13 with `VGA-OK` in T5/T6 gates.

## Hard lessons

- Prove hard requirements before building: attaching the device
  first (10-minute recon) saved a dead-end virtio milestone.
- Mirror at the lowest choke point (serial_putc): zero per-caller
  changes, whole system visible.

## Consequences

- virtio-net stays on the roadmap pending a recipe decision (NIC +
  net stack = its own phase, not a drive-by). No virtio code ships.
- Colors/escape sequences stay future work (single 0x07 attribute).
- Next: Phase 8 input/display polish or Phase 9 planning.
