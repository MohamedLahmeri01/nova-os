# Build and boot NOVA OS

The guest runs x86-32 BIOS code regardless of the host OS. The reference
**build host** is x86-64 Windows with Git, Python 3.11, mingw-w64 GCC
with working `-m32` support and GNU `objcopy` on `PATH`. The linker uses
PE objects; a native Linux ELF compiler is not a drop-in replacement.
For the VM steps use VirtualBox 7.x on an x86-64 host.

## One-disk live session

1. From the repository root run `python tools/nova.py env` and
   `python tools/nova.py build`. This produces `images/nova-os-live.raw`.
   Neither command creates a VM or modifies the diagnostic data disks.
2. With `VBoxManage` on `PATH` (or using its full path), run:

   ```sh
   VBoxManage convertfromraw images/nova-os-live.raw images/nova-live.vdi --format VDI
   ```

3. Create a new **Other (32-bit)** VM, skip unattended installation and
   leave EFI off. Select one CPU, 128 MiB RAM, a PS/2 keyboard and no
   network adapter. Attach the new VDI to SATA port 0 as the only disk.
   For the serial transcript enable COM1, **Raw File**, using an absolute
   path to a new log (the automated test uses this configuration).
4. Boot and click the display to capture the keyboard. Look for
   `LIVE: RAM only`, the init banner and the `> ` prompt. Try:

   ```text
   help
   cat /hello.txt
   put /note hello
   cat /note
   run /bin/hi
   ```

   The first `cat` prints `hello from nova ramfs`; the second prints
   `hello`. The child reports `HI-OK` and its process ID. Right Ctrl
   releases the keyboard. Power off the VM when finished. Nothing
   written in this profile survives reboot. Disk `fsck` returns
   `E19` (no device); external networking is unavailable.

To exercise this configuration automatically after building, use the
[live boot smoke test](../tests/integration/README.md).

## Four-disk diagnostic profile

Use a **disposable checkout**. This profile creates
`NOVA-OS-CONSTRAINED` (2 CPUs, 1024 MiB, BIOS/PIIX3, virtio-net NAT).
`image` and the fault boot rebuild fixture VDIs; guest self-tests also
write fixed scratch locations. These disks are for test data.

```sh
python ci/gate.py
```

The gate builds the live disk, creates the diagnostic fixtures when
VirtualBox is present, then runs the full suite. To inspect an individual
stage, use `image`, `run-vbox`, `panic-test`, or `test` as the subcommand
to `python tools/nova.py`. The last diagnostic transcript is
`build/serial.log`. Missing VM coverage is **PARTIAL** (exit 2).

If the driver refuses an existing VM, check its configuration path and
power state. It must belong to this checkout's `vms/` directory, be
powered off, and use the expected fixture disks for the diagnostic boot.
The already-packaged v0.8.0 beta has its own
[four-image installation guide](INSTALLATION-AND-FIRST-BOOT.md).
