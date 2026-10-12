# NOVA OS v0.8.0 — External Beta Contract

Status words used here: IMPLEMENTED (in the image)
· TESTED (proven this campaign) · PARTIAL (works
with stated limits) · NOT TESTED · NOT IMPLEMENTED.

## Product and audience

NOVA OS v0.8.0, experimental 32-bit x86 operating-
system prototype. Audience: developers and testers
comfortable with VirtualBox and a command line.
Not for end users, not for production, not for
real hardware, not for real data.

## Supported configuration (only this)

- x86-64 host, VirtualBox 7.x, BIOS/PIIX3 VM,
  2 CPUs, 1024 MB RAM, IDE disks, NAT networking.
- 4 disk images (each exactly 1048576 bytes):
  `nova-os.raw` (boot), `fat32.raw` (/disk),
  `ext4.raw` (read-only /ext), `nova.raw` (/nova).
- No physical-hardware support exists. Emulation
  only.

## What works (IMPLEMENTED + TESTED)

- Boot to a 40×25 text console with banner,
  spawn proofs, shell prompt, cursor.
- 37 shell commands (help/uptime/about/stat/
  files/net/diagnostics), bounded input,
  clean errors, `!!` repeat, documented exit.
- Reboot-persistent files on /disk (FAT32) and
  /nova (NOVA-FS): put/cat/rm/ls proven across
  genuine poweroff + cold boot, multi-file,
  repeated cycles. `fsck` clean.
- Fault path: deliberate-fault image panics with
  trap classification + trace tail (dev harness).

## Partial / limits (PARTIAL)

- Overwriting with shorter text leaves the old
  tail (no truncation; documented, by API design).
- FAT names show 8.3 UPPERCASE (`v8.txt`→`V8.TXT`).
- 64 KB per file; 256 B paths; 32 B names.
- Boot selftests rewrite fixed scratch files on
  the data volumes every boot (user files safe).
- NAT gateway ping/DNS proven; external hosts
  NOT TESTED. UDP only; no TCP/TLS.

## Not tested / not implemented

- NOT TESTED: mid-write power loss, external
  internet hosts, files near 64 KB end-to-end,
  any physical machine, any other hypervisor.
- NOT IMPLEMENTED: SMP, mouse, USB, TCP, crypto,
  secure boot, OTA, Linux ABI, pipes/history/
  completion, power management, sound/GPU.

## Data-loss rules (read before booting)

- Test ONLY inside a disposable VM using ONLY
  the supplied images. Never attach a real disk.
- Never share one image between two VMs.
- The volumes are test fixtures: back up anything
  you care about from OUTSIDE (copy the .raw while
  powered off). Abrupt power loss is NOT safe for
  data (clean poweroff is; resets are not).

## Reporting problems

Use FEEDBACK-TEMPLATE.md. Submit through the
repository issue tracker (channel chosen by the
project owner — no external service is set up).
Include: version + artifact SHA-256, host OS +
VBox version, VM config, checksum result, steps,
expected vs actual, serial log excerpt (`VBoxManage
showvminfo` + COM1 file if configured), whether a
fresh VM reproduces it. Redact credentials, tokens,
personal data, and unrelated host details.

## License and redistribution

Apache License 2.0 (LICENSE file included — keep
it with any redistribution, with attribution).
No other licenses apply to the shipped bytes:
the console font loads at runtime from the VGA
BIOS (no vendored font data); no third-party
kernel code is included. Build scripts are
stdlib-only Python + mingw-w64 gcc (versions in
the manifest). Copyleft network: none known —
report any contrary finding as a blocker.
