# ADR-0013: Interrupts and timer — IDT, 8259, PIT, LAPIC bring-up

Status: accepted (Phase 3). Date: 2026-09-29.

## Context

Phase 3 needs CPU exceptions, IRQ delivery, and a timer tick on the
2 CPU / 1GB VirtualBox box, using only on-hand tools.

## Decision

48-entry 32-bit IDT (0-31 exceptions via GAS-macro stubs with correct
error-code behavior, 32-47 IRQs), all DPL0 interrupt gates; exceptions
fatal into the panic handler with vector name, error code, EIP/CS/
EFLAGS (+CR2 on page faults). 8259 remapped to 0x20/0x28, only IRQ0
unmasked; spurious 7/15 EOI'd per spec. PIT channel 0 mode 3 at 100Hz;
liveness proven by observing ticks (hlt-wait, `TIMER-OK`), not by
assuming programming worked. LAPIC probed (base/BSP/enabled) and put
in virtual-wire mode (LINT0 ExtINT, LINT1 NMI, TPR=0, SIVR on) with a
large-page MMIO mapping helper (`vmm_map_mmio`) reusable for later
device MMIO. A `ud2` in the fault image proves the full
hardware→IDT→C→panic chain (`TRAP vec=6 (UD)` → `trap-UD`).

## Evidence

`nova test` 7/7: T5 requires IDT-OK + TIMER-OK from the guest; T6
requires the ud2 chain with correct trap fields.

## Hard lessons (all fixed, all with regression coverage)

- PIC mask 0xFD masks IRQ0 (bit 0) and opens IRQ1: the timer died
  silently with everything else perfect (IRR latched, LINT/TPR/SIVR/
  IF/IDT all verified good across many boots). Readback checks must
  assert independently-chosen constants, never echo the code's own.
- Post-PG table edits need invlpg + CR3 reload (hypervisor shadows
  may ignore invlpg); discovered via a not-present fault on a proven-
  correct LAPIC PTE.
- C trap frames must account for the call's own return-address push;
  verified by raw stack forensics, not by re-reading the code.
- `VMM-PG`-style progress prints between privileged steps localize
  triple faults to the exact instruction (no IDT yet at that point).

## Consequences

- LAPIC stays a probe (no APIC-mode IRQ routing); APIC enable + SMP
  bring-up belong to Phase 4.
- PF/GP branches beyond UD are compiled + reviewed; real-fault
  exercise arrives with user mode and drivers.
- A fully dead PIT would sleep in hlt (no watchdog yet): accepted and
  documented, revisited with Phase 4+ timer infrastructure.
