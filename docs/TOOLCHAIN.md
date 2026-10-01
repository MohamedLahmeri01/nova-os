# NOVA OS — Toolchain (Phase 1, host-clean)

Status: verified on the dev host (see `docs/ENVIRONMENT.md`). No installs,
no PATH/registry changes, no WSL distros, no QEMU — everything below uses
tools already present.

## Components (all pre-existing)

| Role | Tool | Version |
|---|---|---|
| C/asm compiler + assembler (GAS) | mingw-w64 gcc | 15.2.0 (`-m32` → `pe-i386` objects) |
| Linker (PE only: `i386pe`) | mingw ld | 2.45, single-section scripts in `boot/*.ld` |
| Object copy (PE → flat binary) | mingw objcopy | 2.45 (`-O binary`) |
| Image assembly (pad, patch, concat) | stdlib Python | 3.11 (`tools/nova.py image`) |
| raw → VDI, VM control | VirtualBox VBoxManage | 7.2.6r172322 |
| Serial capture | VBox UART file backend | COM1 → `build/serial.log` |

## Pipeline

```text
boot/stage1.S + boot/stage2asm.S --(gcc -m32 -c)--> .o (pe-i386)
kernel/*.c      --(gcc CFLAGS32, -I kernel)-------> .o
stage1.o        --(ld -T boot/stage1.ld @0x7C00)--> stage1.pe
stage2*.o       --(ld -T boot/stage2.ld @0x10000)-> kernel.pe
*.pe            --(objcopy -O binary)-------------> *.bin
stage1.bin + kernel.bin --(python: NVSC patch, AA55)--> images/nova-os.raw
raw --(VBoxManage convertfromraw)--> images/nova-os.vdi --(IDE P0D0)--> TEST_VM
```

Layout is asserted, not assumed: stage1 ≤ 510 B, `NVSC` patch point
present, kernel starts with the stage2 `push` (`0x68`), marker string
present, kernel ≤ 127 sectors. Any violation fails the build loudly.

## Why this shape (and what changes later)

- No ELF cross-gcc, nasm, grub, or xorriso on the host, and the project
  policy forbids silent installs — so BIOS boot via GAS + PE→binary +
  raw images + VDI, all with on-hand tools.
- `ld` cannot emit ELF here; custom scripts merge code into one flat
  `.text` (+ `.datax` after a 4KB boundary for the W^X-ready split);
  `objcopy -O binary -j` dumps load sections so output == memory image.
  `.bss` is NOLOAD (zeroed by stage2asm at entry, never in the image).
- 16-bit stage1 uses `.code16` under `gcc -m32 -c`; all labels link
  absolute (0x7C00 / 0x10000 bases), no relocations survive.
- Symbol rule (mingw `-m32`, learned the hard way): C `foo` becomes
  object `_foo`; assembly labels are literal. So C must declare
  `foo` to match asm `_foo`, and asm must reference `_bar` to match
  C `bar`. Writing `_foo` in C produces `__foo` (double underscore)
  and fails to link with a misleading "undefined reference". When an
  asm label must be visible under both spellings, define both
  (`irq_stub_table` + `_irq_stub_table`, like the linker-script
  `text_end`/`_text_end` twins).
- Later (user-approved): ELF cross-toolchain or Rust `*-unknown-none`
  targets, Multiboot2/GRUB or UEFI — decided by ADR, never by drift.

## Reproduce

```sh
python tools/nova.py env    # read-only capability check
python tools/nova.py build  # skeleton + freestanding + phase1 binaries
python tools/nova.py image  # raw + VDI + attach to NOVA-OS-CONSTRAINED
python tools/nova.py test   # T1-T7 (T5 boot+PMM+VMM+HEAP, T6 panic, T7 heap)
python tools/nova.py run-vbox    # boot once, verify markers, poweroff
python tools/nova.py bench-pmm   # bitmap vs buddy shootout (host, Phase 2a)
```
