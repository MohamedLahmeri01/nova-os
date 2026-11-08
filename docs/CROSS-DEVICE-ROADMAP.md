# Cross-device roadmap (P1 / experimental)

This document extends the current NOVA v0.8 experimental x86-32/BIOS/VirtualBox baseline. **No physical PC, handheld or phone is presently supported by this milestone.**

## Platform targets and sequencing

| Target | CPU / firmware | First input | Key blockers |
|---|---|---|---|
| VirtualBox reference | x86-32 BIOS | PS/2 keyboard/mouse | Keep deterministic regression baseline |
| PC prototype | x86-64 UEFI | USB HID keyboard/mouse | 64-bit paging, UEFI boot, ACPI, USB, storage, modern graphics |
| Gaming handheld prototype | x86-64 UEFI, named model required | Gamepad / touchscreen | All PC blockers plus controller HID, power/thermal, rotation, audio |
| Phone research port | ARM64 UEFI/DT or vendor boot, named unlockable board | Touchscreen | AArch64 exception levels/MMU, board support, display/touch, storage, power, radio; proprietary firmware and locked bootloaders |

## Architecture boundaries

- `arch/<isa>`: CPU entry, traps, MMU, barriers, context switching, atomics.
- `platform/<board>`: firmware discovery, IRQ routing, clocks, power state, IOMMU policy.
- `drivers/`: bus, display, input, storage and network adapters, no implicit access to unrelated hardware.
- `kernel/platform/device_profile.h`: **pure UI input policy only**, does not probe hardware, select firmware, enumerate drivers or imply a bootable platform.
- `userspace/`: portable app ABI and UI surfaces for desktop, controller and touch.

Avoid unreviewed execution of phone flashing, bootloader unlocking, repartitioning, firmware writes or physical-machine installation. Prefer QEMU/VM targets and recoverable development hardware.

## Milestones / exit gates

1. **P1: Device profile policy (this PR).** Independent, freestanding C header; host assertions; no kernel integration, no boot regression claim.
2. **P2: Integration.** Connect a read-only, platform-supplied profile to UI policy via a versioned API; verify existing x86 VM has identical serial traces and existing guest suite remains green.
3. **P3: x86-64 UEFI in QEMU.** Boot and serial markers, exception/MMU tests, page permissions, no reliance on BIOS; maintain x86-32 fixture in CI.
4. **P4: PC drivers.** Enumerate ACPI/PCI, implement USB HID and safe framebuffer backend on named reference hardware; independent CI and device test logs.
5. **P5: Handheld.** One named, recoverable device with gamepad/touch input, rotation and battery monitoring, then UI integration; no claims about commercial game compatibility.
6. **P6: ARM64 virtual platform.** QEMU virt + AArch64 boot, exception and page-table tests; explicit syscall ABI migration plan.
7. **P7: Phone board.** Select a documented unlockable development device; inventory firmware, panel, touch, storage, GPU, modem and power support before any installation work.

## P1 local host check

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I. tests/unit/test_device_profile.c -o /tmp/nova-profile-test
/tmp/nova-profile-test
```

This is a *host-only* test (exit code 0 = pass). It does not replace `python tools/nova.py test` and is intentionally excluded from guest build until P2 has integration evidence.
