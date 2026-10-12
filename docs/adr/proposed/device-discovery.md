# Proposed ADR — Device discovery and firmware interfaces (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

Discovery today: PCI scan with VBox VID:DID
match; memory map via E820 into NOVOSBOOT;
framebuffer mode set by stage1. Options:
(a) producer/consumer rule — boot/plat code
normalizes firmware data (E820, UEFI, device
tree) into the versioned NOVOSBOOT struct;
kernel NEVER parses firmware tables; PCI enum
grows into a real enumerator feeding the driver
registry; (b) kernel parses firmware directly
(reject: forks per firmware, untestable);
(c) static board tables only (reject past one
board: doesn't scale, hides discovery bugs).

## Trade-offs

(a) keeps the firmware-decoupled design that
already works and extends it to UEFI/DT without
touching consumers. Costs: struct versioning
discipline + per-producer tests (malformed tables
as corpus, like wire seeds).

## Prerequisites / evidence to decide

NOVOSBOOT version policy (minor additions only);
second producer (UEFI or DT stub) proving the
consumer unchanged; malformed-firmware corpus.
