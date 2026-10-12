# NOVA OS v0.8.0 — External Beta Release Notes

A 32-bit x86 operating-system prototype you can
boot in VirtualBox, drive from a text console,
and store files on across reboots. For
developers and testers. Not production, not an
OS replacement, emulation only.

## What is new since v0.5

- Reboot-persistent files: `put`/`cat`/`rm`/`ls`
  on `/disk` (FAT32) and `/nova` (NOVA-FS)
  survive genuine poweroff + cold boot
  (verified multi-file, repeated cycles).
- Text console on screen (40×25, cursor,
  scroll), keyboard-only, no mouse by design.
- Self-explanatory shell: `help` lists all 37
  commands, `help <cmd>` usage, `!!` repeats,
  `uptime` + `about` diagnostics, clean errors.
- Every-boot lifecycle battery (`PERSIST-OK`)
  plus the established 23-test suite and gates.

## What it is

Single privilege domain, strict-priority
scheduler, validated syscall boundary, UDP/
DHCP/DNS networking, ramfs + FAT/EXT readers +
writable FAT/NOVA-FS grafts, serial-mirrored
console, Apache-2.0 licensed, stdlib-only build.

## What it is not (read the contract)

No SMP, TCP, TLS, crypto, secure boot, OTA,
USB, mouse, Linux ABI, pipes, power management,
crash-proof storage, or physical-hardware
support. Clean-reboot persistence is proven;
mid-write power loss is explicitly untested.

## Start here

1. `docs/EXTERNAL-BETA-QUICKSTART.md` (20 min).
2. `docs/EXTERNAL-BETA-CONTRACT.md` (the rules).
3. `docs/KNOWN-ISSUES.md` (read before filing).
4. `docs/FEEDBACK-TEMPLATE.md` (to report back).
