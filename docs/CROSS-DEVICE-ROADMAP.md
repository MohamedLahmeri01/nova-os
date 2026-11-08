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
2. **P2: Compositor integration (implemented, verification pending).** Software-supplied profiles select desktop/controller/touch chrome and cursor policy on the existing 320×200 compositor. Existing x86 VM default stays desktop; execute existing VM suite and compare traces before marking complete.
3. **P3: x86-64 UEFI in QEMU (in progress).** P3.1 implements a validated UEFI memory-descriptor normalizer and host tests; it does not boot. P3.2 adds the Clang/LLD PE32+ BOOTX64.EFI entry smoke target (local PE format verified); QEMU/OVMF serial proof remains pending. Next: firmware boot proof, ExitBootServices and 64-bit MMU/exception bring-up. Maintain x86-32 regression fixtures.
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

## P2 API boundary

`comp_configure_profile()` snapshots a validated software-supplied profile, refusing non-320×200 geometry without mutation. `comp_ui_mode()` selects rendering policy. No actual touch/gamepad drivers, new syscall, device detection, or phone boot support are implemented.
