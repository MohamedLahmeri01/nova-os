# Proposed ADR — Hardware portability boundaries (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Port I/O, asm, and board assumptions sit inside
subsystem files (irq.c, pit.c, kbd.c, virtio).
Options: (a) five layers — portable core,
arch/<isa>, plat/<board>, drv families, plat
manifests — with per-boundary contracts (owner,
API, init order, errors, lifetime, concurrency,
IRQ bans, DMA assumptions, stub-testability,
documented deltas); (b) arch-only split (reject:
repeats the scatter at board level); (c) full
platform HAL hiding everything (reject: thick
shims hide costs and rot).

## Trade-offs

(a) makes ports additive and reviewable; costs
up-front extraction with zero-behavioral-delta
proof (same serial bytes). The M1 portability
test (declared file lists, asm-outside-arch grep
gate, two-backend build) is part of this ADR.

## Prerequisites / evidence to decide

Inventory of every arch/plat touch (done: HW
survey in the post-v0.5 audit); stub backend
pattern proven on one subsystem first.
