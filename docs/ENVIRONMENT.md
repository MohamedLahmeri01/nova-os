# NOVA OS — Development Environment Report

Date: 2026-09-29. Method: read-only inspection (no host changes).
All paths below were probed, nothing installed or modified.

## Host

- OS: Windows 11 Professionnel 10.0.26200, 64-bit
- CPU: AMD Ryzen 5 5500, 6C/12T, AddressWidth 64
- RAM: ~16 GiB (17067626496 bytes)
- Disk F:: ~53.6 GiB free (project drive; all writes stay in F:\NOVA_OS)

## Toolchain (verified via --version / probe compiles)

| Tool | Result |
|---|---|
| git 2.50.0.windows.1 | present |
| gcc mingw-w64 15.2.0 (x86_64-w64-mingw32, UCRT) + ld 2.45 + objcopy/objdump | present; `-m32` probe OK; `-ffreestanding -nostdlib` probe OK |
| MSVC 19.44.35228 (Hostx64/x64) | present |
| gdb 16.3.90 | present |
| python 3.11.2 | present |
| cmake 4.2.1, ninja 1.13.0 | present |
| rustc/cargo 1.98.1, toolchain stable-x86_64-pc-windows-msvc | present; bare-metal targets NOT installed (available: x86_64-unknown-none, i686-unknown-*, aarch64-unknown-none, riscv64gc-unknown-none-elf) |
| clang (as `clang`), nasm, make, grub-mkrescue, xorriso | NOT on PATH; MSYS2 base present at C:\msys64 but no toolchain packages installed in msys/ucrt64 shells |
| MSYS2 | C:\msys64 base install only (no gcc/make/nasm/qemu in any shell) |

## Virtualization / testing

- VirtualBox 7.2.6r172322 at F:\VirutalBox\ (registry InstallDir).
  `VBoxManage --version`, `list hostinfo`, `list vms` all succeed.
  Hostinfo: HW virtualization yes, nested paging yes, long mode yes,
  unrestricted guest no. Existing VMs: "Linux-CentOs", "Google Pixel 3a"
  — MUST NOT be modified; test VMs get `NOVA-OS-*` names.
- QEMU: `qemu-system-x86_64` NOT found. Only stale `qemu-img` 2.12.0
  from Android SDK emulator dir. QEMU 11.1.0 available via winget but
  NOT installed (host-change policy: do not install without approval).
- Hyper-V: enabled. WSL: present, no distros installed.
- VirtualBox is therefore the Phase 1 test target.

## Cross-compiler status

- No ELF cross-gcc (i686-elf / x86_64-elf) on PATH. mingw-w64 gcc can
  emit freestanding objects but targets PE/COFF by default; ELF output
  for kernel images requires either an ELF cross-toolchain or
  Rust `*-unknown-none` targets or LLVM. Decision deferred to Phase 1
  ADR (bootloader protocol + image format), with a documented manual
  setup recipe — no silent host installs.

## Gaps / blockers (honest)

1. No assembler (nasm) on PATH → Phase 1 needs one; recipe documented,
   install is manual, user-approved only.
2. No QEMU → boot tests run on VirtualBox only until user approves QEMU.
3. No ELF cross-toolchain → Phase 1 must pick: Rust bare-metal vs
   cross-gcc vs clang/lld, each with a no-host-pollution install path.
4. No grub/xorriso/mtools → ISO generation deferred; Phase 1 starts
   with raw/ISO via Python-only tooling where possible.

## Policy going forward

`tools/nova.py env` reproduces these checks read-only. Any step that
would install software, change PATH/registry, reboot, touch other
drives, or modify existing VMs is forbidden without explicit approval.
