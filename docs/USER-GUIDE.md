# NOVA OS v0.8.0 — User Guide

NOVA OS is an experimental 32-bit x86 operating
system: a text console, a small shell, files that
survive reboot, and basic local networking. It is
a prototype for learning and testing — not a daily
driver, not production, not an OS replacement.

## What it can do

- Boot in VirtualBox (BIOS, 2 CPUs, 1024 MB) to
  a 40×25 text console in ~20 seconds.
- Run 37 shell commands: help, files, diagnostics,
  UDP/loopback network probes, DNS lookups.
- Keep files you put on `/disk` (FAT32) or `/nova`
  (NOVA-FS) across genuine poweroff + cold boot.
- Report errors plainly (`unknown`, `denied`,
  `failed E<num>`) and faults loudly (panic screen
  with evidence, never silent corruption).

## What it cannot do

- No mouse, no graphics beyond text, no sound.
- No TCP — so no HTTP, no HTTPS, no SSH, no
  downloads. Network reach ends at UDP/DNS/ICMP
  to the local virtual network (details in
  NETWORKING-AND-INTERNET-TESTS.md).
- No SMP (one CPU really works), no USB, no Wi-Fi,
  no power management, no Linux programs, no
  pipes, no command history (use `!!` to repeat),
  no scripting. 64 KB per file. `exit` ends the
  session (reset the VM for a new shell).

## Required software and VM configuration

- 64-bit host with VirtualBox 7.x.
- A disposable VM: BIOS/PIIX3, 2 CPUs, 1024 MB,
  IDE controller, NAT with virtio network card.
  Full steps: INSTALLATION-AND-FIRST-BOOT.md.
- 4 MB of disk images (supplied): boot + three
  1 MB data disks. Never substitute a real disk.
  Never share one image between two VMs.

## Using the shell

Click inside the VM window first (else your keys
go to the host; Right Ctrl releases). Then:

```text
help                 list all 37 commands
help cat             what one command does + usage
about                version, target, ABI number
uptime               ticks and nominal seconds
stat                 live counters (ticks, threads…)
ls /disk             list the persistent volume
put /disk/n.txt hi  write a file (nothing printed = ok)
cat /disk/n.txt      read it back
rm /disk/n.txt       delete it (ok = gone)
ping                 ping the gateway
nslookup example.com resolve a public name (NAT DNS)
```

`/` and `/test` are memory-only (gone on reboot);
`/disk` and `/nova` persist. Overwriting with
shorter text leaves the old tail (no truncation —
rewrite via rm + put for exact contents). FAT
names show UPPERCASE 8.3 (`n.txt` → `N.TXT`).

## Shutting down and restarting

There is no shutdown command and no ACPI handling.
The supported clean procedure: wait for the `> `
prompt (writes are synchronous — a returned prompt
means bytes are on disk), then host-side
`VBoxManage controlvm <vm> poweroff`. Confirm
`VMState="poweroff"`, then start again. Forced
reset mid-command is untested territory: assume
nothing about data written in the interrupted
command.

## When something breaks

TROUBLESHOOTING.md first, then KNOWN-ISSUES.md
(ten confirmed behaviors that are not bugs), then
the feedback template with a screenshot or serial
excerpt. Back up valued test files from outside
the VM (copy the .raw while powered off).
