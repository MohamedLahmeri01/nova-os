# NOVA OS v0.5 — Release Manifest

NOVA OS v0.5

Source commit:
(see git log — Phase L `feat(release)` commit)

Build date:
2026-10-08 (UTC)

Toolchain:
mingw-w64 gcc 15.2.0 `-m32` → pe-i386 → ld custom
scripts → objcopy -O binary; `CFLAGS32` includes
`-mno-sse -mno-mmx`; Python 3.11 `tools/nova.py`;
`ci/gate.py` = lint + build + image.

Target:
x86 (32-bit guest), BIOS / PIIX3 / PIC.

VM validation:
VirtualBox 7.2.6r172322, VM NOVA-OS-CONSTRAINED
`--cpus 2 --memory 1024`, IDE PIO/DMA disk,
virtio-net NAT, UART COM1 → build/serial.log.
Do not run outside this envelope without
re-validation (see report §29).

Mandatory gates:
PASS (23/23, CORPUS-OK 7/7, GATES-OK,
TRACEFETCH-OK, TRACEFETCH-ADV-OK, RUNTIME-OK,
FSSTRESS-OK, NETSTRESS-OK, THREX-OK, XSUB-OK,
PERF-OK, LINT-OK, GATE-OK, panic-test tick
equality, 135 s soak, 4 cold boots).

Security audit:
CONDITIONAL — contracts hold (§28); ring-0
drivers, no NX/ASLR/IOMMU/SMP are documented
accepted gaps, not defects.

Integration audit:
PASS — scenarios A–F green (§18), exhaustion
matrix green (§19), recovery green (§21).

Known limitations:
§29 of the Phase L report (SMP parked, PIC,
TSC deferred, VFS big lock, no reassembly,
bounded net, minimal shell, no NX/ASLR, 256-
event trace, no reaping, no fault recovery, no
double-fault handler, fuzzy stats, no wall
clock, no OOM injection, no record/replay).

Release status:
CONDITIONALLY RELEASE READY
