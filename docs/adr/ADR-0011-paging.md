# ADR-0011: Paging — classic 32-bit (non-PAE); PAE rejected by evidence

Status: accepted (Phase 2b). Date: 2026-09-29.

## Context

Phase 2b wanted PAE identity-mapping with NX-by-default. The code was
built (PDPT+PD+PT, XD pages, EFER.NXE) and died deterministically:
triple fault (Guru 1155) at `mov %eax,%cr0` enabling PG, CR0.PG never
taking effect. Environment: VirtualBox 7.2 NEM (Hyper-V backend), AMD
Ryzen 5 5500, 2 CPU / 1GB guest.

## Elimination log (each a full image+boot cycle, same death EIP)

1. Full PAE + NXE + XD → dies.
2. PAE, NXE=0, no XD anywhere → dies (NXE/XD ruled out).
3. Minimal PAE (two 2MB large pages, rest absent) → dies (table
   contents ruled out).
4. PAE all-4KB, no PS pages → dies (large pages ruled out).
5. CR3→CR4 reorder to canonical CR4→CR3 → dies (order ruled out).
6. +PSE → dies. 7. +WP → dies (flag validators ruled out).
8. CR0 no-op write (same value) → WORKS, prints continue (CR0 writes
   per se accepted; only the PG transition is rejected).
9. Non-PAE classic (PD + PT + 4MB page) → WORKS FIRST TRY.

Guru register forensics (VBox.log): death EIP == the mov-cr0 opcode
byte (verified against objdump), CR0=0x11 (PG never set), CR3=PDPT,
CR4.PAE set, EFER.NXE set, CPL0, IF=0. Tables re-audited legal
(alignment, reserved bits, XD only with NXE). Conclusion: the NEM
hypervisor rejects this guest's PAE PG-enable; root cause inside
closed firmware/hypervisor, not actionable from our side.

## Decision

Ship classic 32-bit paging: PD + PT for [0,4MB) with null guard and
stack guard, 4MB large pages (PSE, gated) to arena end rounded up,
upper PD entries zero (user half + MMIO future). PAE explicitly
cleared for determinism regardless of firmware residue.

## Consequences (honest)

- No XD without PAE: kernel text/data are RW (no W^X yet). The
  .text/.datax link split + text_end symbol STAY (ready for NX).
- NX slides to the x86-64 long-mode milestone, where PAE/NX are
  mandatory. Revisit trigger: re-prove PAE under QEMU CI or long mode;
  this ADR must be superseded, not silently ignored.
- `kernel/arch/*/cpu.h` (CPUID/CR/MSR backends) stays: still the only
  home for privileged CPU control.
