# P3.1 — UEFI memory descriptor normalization (x86-64 preparation)

**Scope: implemented and host-tested; not a bootable UEFI image.**

The existing x86-32 BIOS boot path and its native-pointer `boot/boot_info.h` remain untouched. P3.1 introduces a separate freestanding, fixed-width region representation and strict firmware-map validation, independent of any physical firmware.

## Contract

- UEFI memory descriptors use firmware-supplied descriptor stride (minimum 40 bytes); offsets: type 0, physical base 8, page count 24.
- Only type 7 (EfiConventionalMemory) becomes usable. Loader, boot-service, runtime, and unknown classes remain reserved until explicit lifecycle ownership is designed. Reserved means *never hand to current PMM*.
- Zero pages, misaligned physical bases, multiplication/addition overflow, overlapping regions, malformed strides, excessive descriptors and insufficient capacity are rejected. On rejection, count becomes zero; no partial map is published.
- Inputs and outputs must be backed by accessible buffers. This function cannot verify memory mappings, trusted firmware pointers or firmware provenance.
- The normalization happens after GetMemoryMap and before transitioning kernel ownership. A future loader must correctly implement ExitBootServices with map-key retry and protect its own image, stacks and reserved tables.

## Reproduce host test

```sh
gcc -std=c11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=undefined -fno-sanitize-recover=all -I. \
  boot/uefi/memory_map.c tests/unit/test_uefi_memory_map.c \
  -o nova-uefi-test
./nova-uefi-test
```

Observed on a Linux/GCC host: exit 0 with UBSan enabled. This does **not** verify UEFI, QEMU, firmware calling conventions, PE/COFF, ELF handoff, 4-level paging or a 64-bit kernel.

## Next P3 gates

1. Create a reproducible x86-64 `BOOTX64.EFI` build under a **new** explicit command, keeping `nova.py build` and the BIOS loader unchanged.
2. Use QEMU + OVMF in a disposable disk, inspect firmware and platform capability, record serial markers.
3. Implement and test GetMemoryMap/ExitBootServices contract, page permissions, ownership of loaded image/stack and framebuffer; avoid reusing old 32-bit pointer ABI.
4. Add kernel 64-bit entry/trap/page-table test and only then test a user process.
5. Keep original x86-32 guest suite and serialized baseline as mandatory regression gates.

Never flash firmware, repartition real disks, or install to physical machines as part of these gates.
