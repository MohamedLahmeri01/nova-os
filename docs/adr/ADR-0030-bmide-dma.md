# ADR-0030: BMIDE DMA — bus-master disk I/O replaces PIO

Status: accepted (Phase 8c). Date: 2026-10-03.

## Context

All disk I/O polled PIO (hundreds of VM exits per sector; spin
budgets tuned around dynamic-VDI stalls). The PIIX IDE function we
enumerate (00:01.1) has a BMIDE unit: PRD tables + READ/WRITE DMA
commands move sectors without CPU-per-word exits.

## Decision

`dma_sector()` in `kernel/drivers/ata.c`: BAR4 from the PCI table,
bus-master + CLS/latency programmed, MWDMA2 transfer mode via SET
FEATURES, PRD builder splitting caller buffers at page/64K bounds,
Intel order (PRD, dir, clear, taskfile, command, START), INT-bit
polling. Selftest proves DMA-vs-PIO byte equality on reads and
write/readback on scratch, then routes ALL traffic via DMA (every
later FS gate runs over it). Absent BMIDE keeps PIO. `DMA-OK` gated.

## Evidence

Guest `ATA-BMIDE base=0000d000` + `DMA-OK` + unchanged FS-OK/FAT-OK/
EXT-OK/NOVA-OK over DMA; `nova test` 13/13 (T5/T6 both gate DMA-OK).

## Hard lessons

- BMIDE direction bit 3 SET = device-to-memory (READS). Intel's
  "Read/Write Control" naming reads backwards from the disk side;
  VBox+QEMU sources agree on the polarity, and VBox asserts it
  (`bm->u8Cmd & BM_CMD_WRITE` in its async thread — the exact
  abort we chased). Having it backwards aborts the VM, not an
  error: direction bugs are fatal, not errno.
- A leftover early-STOP from bisecting looked like UART reordering
  (DONE before GO): audit prints after every edit, not just code.
- VBox UART file mode appends: always delete serial.log before a
  manual boot or tails mix runs.
- VBox.log dates are DD/MM (fresh, not stale); the abort reason
  (DevATA.cpp assertion + location) was in there all along.
- MWDMA2 over UDMA2 (needs no UDMA controller registers); CLS +
  latency programmed regardless (BIOS leaves both zero).

## Consequences

- Polling stays (no IRQ-driven DMA yet); PRD table is one page;
  transfers stay single-sector (callers already chunk).
- Next: 8d virtio (attach a virtio device first — no virtio
  hardware exists on the VM today), then display/input drivers.
