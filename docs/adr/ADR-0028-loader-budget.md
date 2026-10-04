# ADR-0028: Loader budget — chunked INT13, cap 768 sectors

Status: accepted (Phase 8a). Date: 2026-10-03.

## Context

The kernel reached 126/127 single-load sectors: the next feature
would not boot. The 127 cap was BIOS practice (max DAP count per
INT13 call), not a memory limit — the pm stack at 0x90000 bounds
the kernel to < 512KB instead (768 sectors / 384KB chosen, ending
0x70000 with margin).

## Decision

stage1 loops INT13/0x42 in <=127-sector chunks (first chunk reuses
the template LBA/seg: zero init cost; later chunks advance both;
+32B, stage1 now 496/510). nova.py caps at 768 with the memory
rationale in the message. Proved by a one-off padded 140-sector
kernel (127+13 across the chunk boundary): full boot to shell +
HI-OK. The loop's 1-iteration path runs every boot forever after.

## Evidence

Padded image booted to `INIT-OK` + shell + `HI-OK pid=5`,
VMState running; rebuilt pristine kernel; `nova test` 13/13.

## Hard lessons

- `nova image` rebuilds (wiping manual padding): one-off proofs
  must assemble without the build step (call internals directly).
- stage1 has 14B left: the NEXT bootloader change needs its own
  diet first (A20-verify and error letters are untouchable).

## Consequences

- Kernel growth unblocked up to 384KB; the 127-sector constant now
  lives only in stage1's chunking, not the tooling.
- Next: Phase 8 PCI enumeration (PIIX3 host bridge), then virtio.
