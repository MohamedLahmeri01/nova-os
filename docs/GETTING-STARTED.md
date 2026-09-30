# Getting started with NOVA OS

This guide takes you from zero to a booting kernel in about five
minutes. Tested on Windows 11 + VirtualBox 7.x.

## 1. Prerequisites

| Tool | Check | Why |
|---|---|---|
| mingw-w64 gcc (with `-m32`) | `gcc -m32 --version` | compiles the 32-bit kernel + boot sector |
| Python 3 | `python --version` | the `nova.py` build/test driver |
| VirtualBox 7.x | `VBoxManage --version` | boots the test VM |

No NASM, no GRUB, no QEMU, no WSL needed. The build installs
nothing and changes nothing outside this repository.

## 2. Verify your host (read-only)

```sh
python tools/nova.py env
```

Expect `[ok]` for git, gcc, python, cmake, rustc, VirtualBox. Missing
optional tools are reported as `[info]`, not failures.

## 3. Build

```sh
python tools/nova.py build
```

Compiles the BIOS stage1 (must fit 510 bytes — asserted), the 32-bit
kernel (freestanding `-m32`, PE→flat-binary via linker scripts), and
checks the layout: boot signature patch point, entry prologue bytes,
marker string, sector budget. Any violation fails loudly.

## 4. Create the disk image + test VM

```sh
python tools/nova.py image
```

Assembles `images/nova-os.raw` (stage1 + kernel + `AA55` signature),
converts it to VDI, and attaches it to a new isolated VM named
`NOVA-OS-CONSTRAINED` (2 CPU, 1024 MB RAM, no NIC, no USB, PIIX3/BIOS)
with COM1 logging to `build/serial.log`. Your existing VMs are never
listed for modification, let alone touched — the driver only ever
addresses `NOVA-OS-*` names.

## 5. Boot it

```sh
python tools/nova.py run-vbox
```

Starts the VM headless, waits up to 30 s for the boot markers on
serial, screenshots, powers off, and reports `[PASS]` with the final
serial output: E820 map, PMM stats, paging summary, heap check,
LAPIC probe, `IDT-OK`, `TIMER-OK`, `NOVA-OS-READY-HALT`.

## 6. Run everything

```sh
python tools/nova.py test      # T1–T7: host checks + 2 gated VM boots
python tools/nova.py bench-pmm # bitmap-vs-buddy shootout (host)
python tools/nova.py panic-test# fault-injection image: ud2 -> trap -> panic
python tools/nova.py vbox-check# verify the 2 CPU / 1 GB budget + VM list
python tools/nova.py clean     # remove build outputs (VM + images kept)
```

## Troubleshooting

| Symptom | Meaning | Fix |
|---|---|---|
| `[blocked] gcc not on PATH` | toolchain missing | install mingw-w64 with 32-bit support |
| `run-vbox` → `[blocked] No image attached` | skipped step 4 | run `nova image` first |
| `timer-dead` panic | no IRQ0 delivery | check VBox logs; historically a PIC mask typo — see ADR-0013 |
| Guru Meditation 1155 | triple fault | read `vms/NOVA-OS-CONSTRAINED/Logs/VBox.log` (EIP + CR0 tell the story) |
| `IMAGE [...] VERR_VD_INVALID_SIZE` | raw < 1 MB | already handled: images are padded (dynamic VDI stays tiny) |

## What's next

- [ROADMAP.md](../ROADMAP.md) — phase plan with honest checkboxes
- [docs/ARCHITECTURE.md](ARCHITECTURE.md) — full system design
- [docs/adr/](adr/) — every major decision with alternatives + evidence
- [CONTRIBUTING.md](../CONTRIBUTING.md) — how to contribute
