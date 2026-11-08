#!/usr/bin/env python3
"""Build and optionally smoke-run isolated x86-64 UEFI image.
Never invokes or changes the legacy NOVA BIOS/VirtualBox build.
Writes only build/uefi/ inside the repository, no host installation.
"""
import argparse
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "uefi"
SOURCES = [ROOT / "boot" / "uefi" / "entry.c",
           ROOT / "kernel" / "arch" / "x86_64" / "early.c"]
MARKER = b"NOVA-KERNEL-X64-ENTRY-OK"

def require(name):
    executable = shutil.which(name)
    if not executable:
        raise RuntimeError(f"missing {name} on PATH")
    return executable

def build():
    clang, linker = require("clang"), require("lld-link")
    OUT.mkdir(parents=True, exist_ok=True)
    objs = []
    efi = OUT / "BOOTX64.EFI"
    for source in SOURCES:
        obj = OUT / (source.stem + ".obj")
        subprocess.run([clang, "-target", "x86_64-pc-windows-msvc",
                        "-ffreestanding", "-fshort-wchar", "-fno-stack-protector",
                        "-fno-builtin", "-Wall", "-Wextra", "-Werror",
                        "-std=c11", "-O2", "-c", str(source), "-o", str(obj)],
                       check=True)
        objs.append(obj)
    subprocess.run([linker, "/nologo", "/machine:x64",
                    "/subsystem:efi_application", "/entry:efi_main",
                    "/nodefaultlib", "/fixed:no", "/out:" + str(efi),
                    *map(str, objs)], check=True)
    blob = efi.read_bytes()
    if len(blob) < 256 or blob[:2] != b"MZ":
        raise RuntimeError("EFI PE DOS header missing")
    pe = struct.unpack_from("<I", blob, 0x3c)[0]
    if pe + 26 >= len(blob) or blob[pe:pe+4] != b"PE\0\0":
        raise RuntimeError("EFI PE signature missing")
    machine = struct.unpack_from("<H", blob, pe + 4)[0]
    opt_size = struct.unpack_from("<H", blob, pe + 20)[0]
    magic = struct.unpack_from("<H", blob, pe + 24)[0]
    subsystem = struct.unpack_from("<H", blob, pe + 24 + 68)[0]
    if machine != 0x8664 or magic != 0x20b or subsystem != 10 or opt_size < 70:
        raise RuntimeError("not an x86-64 EFI application")
    if MARKER not in blob:
        raise RuntimeError("serial proof marker missing")
    print(f"UEFI-PE-OK {efi} ({len(blob)} bytes)")
    esp = OUT / "esp" / "EFI" / "BOOT"
    esp.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(efi, esp / "BOOTX64.EFI")
    return esp.parent.parent

def run_qemu(esp, firmware, vars_template, timeout):
    qemu = require("qemu-system-x86_64")
    if not firmware.exists() or not firmware.is_file():
        raise RuntimeError("OVMF firmware file not found")
    # Read-only virtual FAT drive, no host block device exposed.
    cmd = [qemu, "-machine", "q35", "-m", "512M", "-smp", "2",
           "-nodefaults", "-nographic", "-monitor", "none",
           "-serial", "stdio", "-no-reboot", "-net", "none"]
    if vars_template is None:
        # Unified OVMF.fd ROM with internal variable storage.
        cmd += ["-bios", str(firmware)]
    else:
        if not vars_template.is_file():
            raise RuntimeError("OVMF_VARS template not found")
        vars_copy = OUT / "OVMF_VARS.test.fd"
        shutil.copyfile(vars_template, vars_copy)
        cmd += ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}",
                "-drive", f"if=pflash,format=raw,file={vars_copy}"]
    cmd += ["-drive", f"format=raw,media=disk,readonly=on,file=fat:ro:{esp}"]
    try:
        result = subprocess.run(cmd, capture_output=True, timeout=timeout)
        serial = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        serial = (e.stdout or b"") + (e.stderr or b"")
    (OUT / "qemu-serial.log").write_bytes(serial)
    if MARKER not in serial or b"NOVA-UEFI-X64-ENTRY-OK" not in serial:
        raise RuntimeError("QEMU marker not observed; see build/uefi/qemu-serial.log")
    print("UEFI-QEMU-KERNEL-ENTRY-OK (marker verified in serial output)")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["build", "test-qemu"])
    ap.add_argument("--ovmf", type=pathlib.Path,
                    help="path to a read-only OVMF_CODE.fd (test-qemu)")
    ap.add_argument("--ovmf-vars", type=pathlib.Path,
                    help="OVMF_VARS template for split CODE/VARS mode")
    ap.add_argument("--timeout", type=int, default=25)
    a = ap.parse_args()
    try:
        esp = build()
        if a.action == "test-qemu":
            if not a.ovmf:
                raise RuntimeError("--ovmf is required for test-qemu")
            if a.timeout < 1 or a.timeout > 120:
                raise RuntimeError("timeout must be 1..120 seconds")
            run_qemu(esp, a.ovmf.resolve(),
                     a.ovmf_vars.resolve() if a.ovmf_vars else None,
                     a.timeout)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as e:
        print(f"[FAIL] {e}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
