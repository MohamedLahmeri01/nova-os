# Proposed ADR — Production filesystem: one journaled FS chosen by evidence (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

RAMFS (test/seed), FAT32/EXT4/NOVA-FS readers
(proven via mutant manifests). Production needs
crash-consistent read/write. Options: (a) grow
NOVA-FS with journaling (keeps the proven VFS +
big-lock story, pays design risk); (b) adopt an
embedded journaled FS (faster to assurance, pays
porting + license review); (c) port a big-system
FS subset (reject: size and complexity dwarf the
team; flash behavior unproven).

## Trade-offs

Decide by: crash-consistency proof cost, flash/
FTL behavior, code size, license, and how much
of the v0.5 VFS contract survives. Either way the
big lock stays until a measured reason (not a
feeling) forces finer granularity — SMP re-audit
owns that question.

## Prerequisites / evidence to decide

M4 crash-injection rig BEFORE the choice (so
candidates are scored by running, not reading);
power-loss test definition; media survey (NOR/
NAND/eMMC differ — one strategy does not fit
all); fsck-after-chaos acceptance bar.
