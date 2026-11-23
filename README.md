# NOVA OS

NOVA is an experimental **32-bit x86 BIOS operating system** written in C
and assembly. It boots its own kernel, runs user programs in ring 3, and
provides a keyboard-driven shell. No Linux kernel or GRUB is required.

## Try the live shell

On the reference Windows x86-64 build host, install Python 3, Git and
mingw-w64 GCC/binutils with `-m32` support, then run:

```sh
python tools/nova.py env
python tools/nova.py build
```

The result is **`images/nova-os-live.raw`**. Building it does not require
VirtualBox. To boot it in VirtualBox 7.x, convert it to a virtual disk:

```sh
VBoxManage convertfromraw images/nova-os-live.raw images/nova-live.vdi --format VDI
```

Attach that VDI as the only SATA disk in a new BIOS VM with one CPU,
128 MiB RAM and networking disabled. [Setup and first session](docs/GETTING-STARTED.md)
has the settings. At the `> ` prompt try:

```text
about
put /note hello
cat /note
run /bin/hi
```

**Live files are in RAM and disappear on reboot.** The live kernel does
not mount data disks. The separate developer profile exercises FAT32,
read-only EXT4, NOVA-FS and virtio networking using disposable fixtures.

## What runs where

| Guest target | Status |
|---|---|
| x86 BIOS, VirtualBox, single boot disk | Live keyboard shell, RAM files and native programs; exercised by T24. |
| x86 BIOS, VirtualBox, four IDE disks + NAT virtio | Diagnostic profile: storage, network, scheduler and isolation tests, including a separate fault boot. |
| Native x86-64 UEFI PCs | No native boot target or qualified hardware. |
| ARM64 phones, tablets and boards; RISC-V | No kernel port, boot image or qualified device. |

This is **not ready to replace a PC or phone's primary OS**. The
[readiness matrix](docs/PRODUCTION-READINESS-MATRIX.md) lists the next
engineering milestones and what would establish support on each target.

## Development and verification

```sh
python ci/gate.py
```

Use a disposable checkout for this command: it rebuilds the diagnostic
VM's fixture disks. The gate runs lint, the build, the host suite, both
diagnostic boots and a live session. Exit `0` means every check ran and
passed; `1` means failure; `2` means required coverage was skipped.

See the [shell manual](docs/CLI.md), [syscall ABI](docs/ABI.md),
[architecture decisions](docs/adr/), [contribution guide](CONTRIBUTING.md)
and [Apache-2.0 license](LICENSE).
