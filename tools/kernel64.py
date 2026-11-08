#!/usr/bin/env python3
"""Build and validate an independently linked experimental ELF64 kernel.

Does not modify the P3.3 UEFI executable, virtual disks, or host boot drives.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "uefi"
SRC = ROOT / "kernel" / "arch" / "x86_64" / "standalone.c"
MARKER = b"NOVA-KERNEL-ELF64-ENTRY-OK"

def require(name):
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"Missing {name} on PATH")
    return path

def validate_elf(path):
    raw = path.read_bytes()
    if len(raw) > 1024 * 1024 or len(raw) < 64:
        raise RuntimeError("ELF file size outside allowed 64B..1MiB")
    if raw[:6] != bytes((0x7f, 69, 76, 70, 2, 1)):
        raise RuntimeError("Not ELF64 little-endian")
    e_type, machine = struct.unpack_from("<HH", raw, 16)
    if e_type != 3 or machine != 62:
        raise RuntimeError("Kernel must be x86_64 ET_DYN ELF")
    entry, phoff = struct.unpack_from("<QQ", raw, 24)
    ehsize, phentsize, phnum = struct.unpack_from("<HHH", raw, 52)
    if ehsize != 64 or phentsize != 56 or not (1 <= phnum <= 16):
        raise RuntimeError("Unsupported ELF header")
    if phoff > len(raw) or phnum * phentsize > len(raw) - phoff:
        raise RuntimeError("Program headers exceed file")
    load_ranges = []
    for i in range(phnum):
        offset = phoff + i * phentsize
        ptype, flags = struct.unpack_from("<II", raw, offset)
        foff, vaddr, _, filesz, memsz, align = struct.unpack_from("<QQQQQQ", raw, offset + 8)
        if ptype == 2:
            if foff > len(raw) or filesz > len(raw)-foff or filesz % 16:
                raise RuntimeError("Malformed PT_DYNAMIC")
            for j in range(foff, foff + filesz, 16):
                tag, _ = struct.unpack_from("<QQ", raw, j)
                if tag in (7, 8, 9, 17, 18, 19, 23, 35, 36, 37):
                    raise RuntimeError("Dynamic ELF relocations are unsupported")
        if ptype != 1:
            continue
        if not filesz <= memsz or foff > len(raw) or filesz > len(raw)-foff:
            raise RuntimeError("Invalid PT_LOAD file range")
        if (flags & 1) and (flags & 2):
            raise RuntimeError("Writable and executable PT_LOAD segment")
        if memsz == 0 or vaddr > 0xffffffff or memsz > 0xffffffff-vaddr:
            raise RuntimeError("Invalid PT_LOAD address range")
        if align not in (0, 1, 4096):
            raise RuntimeError("Unexpected PT_LOAD alignment")
        load_ranges.append((vaddr, vaddr+memsz, flags))
    if not load_ranges or not any(lo <= entry < hi and flags & 1 for lo, hi, flags in load_ranges):
        raise RuntimeError("ELF entry outside executable PT_LOAD segment")
    if MARKER not in raw:
        raise RuntimeError("Kernel serial marker missing")
    return len(raw), len(load_ranges)

def main():
    clang, linker = require("clang"), require("ld.lld")
    OUT.mkdir(parents=True, exist_ok=True)
    obj = OUT / "standalone-kernel.o"
    elf = OUT / "nova-kernel-x64.elf"
    subprocess.run([clang, "-target", "x86_64-unknown-none-elf",
                    "-ffreestanding", "-fpie", "-fno-stack-protector", "-fno-builtin",
                    "-fno-asynchronous-unwind-tables", "-fno-exceptions",
                    "-Wall", "-Wextra", "-Werror", "-std=c11", "-O2",
                    "-c", str(SRC), "-o", str(obj)], check=True)
    subprocess.run([linker, "-pie", "-nostdlib", "-z", "max-page-size=0x1000",
                    "-e", "nova_kernel_entry", "-o", str(elf), str(obj)], check=True)
    size, loads = validate_elf(elf)
    # This is build-time only: the EFI loader is NOT wired to this file yet.
    print(f"NOVA-KERNEL-ELF64-BUILD-OK {elf} ({size} bytes, {loads} load segments)")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        sys.exit(f"[FAIL] {exc}")
