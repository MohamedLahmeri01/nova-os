#!/usr/bin/env python3
"""Qualify the BIOS images. Exit 0: all ran; 1: failure; 2: incomplete."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from nova import VBOX


def main():
    steps = ["env", "lint", "build"]
    if VBOX:
        steps.append("image")
    else:
        print("[gate SKIP] VirtualBox unavailable: guest boots require a VM")
    for step in [*steps, "test"]:
        print(f"== gate: {step} ==", flush=True)
        r = subprocess.run([sys.executable, ROOT / "tools/nova.py", step], cwd=ROOT)
        if step == "test" and r.returncode == 2:
            print("[gate PARTIAL] required coverage skipped")
            return 2
        if r.returncode:
            print(f"[gate FAIL] {step} -> {r.returncode}")
            return 1
    print("[gate PASS] host tests, diagnostic boots and live session")
    return 0


if __name__ == "__main__":
    sys.exit(main())
