#!/usr/bin/env python3
"""NOVA OS local/CI gate (Phase 0.3-F): host-side checks, no VM.

Runs: lint (syntax + py_compile + ADR continuity + no empty dirs),
build (freestanding compile + link + layout asserts), image
(raw+VDI assembly). The full suite (`nova test`, incl. VBox boots)
runs on dev machines and nightly; this gate is the portable,
every-commit subset (a GitHub runner with mingw-w64 + Python 3.11
+ VirtualBox can extend it to `test` when available).

Exit nonzero on the first failing step. Verified on the reference
host (Ryzen 5 5500, Win11, mingw 15.2.0) 2026-10-06.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOVA = [sys.executable, os.path.join(ROOT, "tools", "nova.py")]


def step(name):
    print(f"== gate: {name} ==")
    r = subprocess.run(NOVA + [name], cwd=ROOT)
    if r.returncode != 0:
        print(f"[gate FAIL] {name} -> {r.returncode}")
        return False
    print(f"[gate ok] {name}")
    return True


def main():
    for name in ["lint", "build", "image"]:
        if not step(name):
            return 1
    print("[gate PASS] lint + build + image (run `nova test` for VM gates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
