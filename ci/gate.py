#!/usr/bin/env python3
"""NOVA OS local/CI gate (Phase 0.3-F, VM-SKIP semantics 0.4-R).

Runs: env (toolchain versions recorded), lint, build, image.
The full suite (`nova test`, incl. VBox boots) runs on dev
machines and nightly; this gate is the portable subset. When
VirtualBox is absent, VM-dependent steps report SKIP explicitly
— never PASS: the final line distinguishes PASS (everything ran)
from PARTIAL (host checks green, VM steps skipped). A GitHub
runner with mingw-w64 + Python 3.11 + VirtualBox can extend to
`test` when available; until a runner executes it, no workflow
file ships (unexecuted CI is decoration).

Verified on the reference host (Ryzen 5 5500, Win11, mingw
15.2.0, VBox 7.2.6) 2026-10-06.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOVA = [sys.executable, os.path.join(ROOT, "tools", "nova.py")]
# Same well-known location nova.py itself uses (plus PATH).
VBOX_KNOWN = r"F:\VirutalBox\VBoxManage.exe"


def env_step():
    print("== gate: env ==")
    ok = True
    for tool in ["git", "gcc", sys.executable]:
        p = shutil.which(tool) if tool != sys.executable else tool
        if p is None:
            print(f"[gate FAIL] missing tool: {tool}")
            ok = False
            continue
        try:
            r = subprocess.run([p, "--version"], capture_output=True,
                               text=True, timeout=30, cwd=ROOT)
            first = (r.stdout or "").strip().splitlines()
            print(f"[gate ok] {tool}: "
                  f"{first[0] if first else 'present'}")
        except Exception as e:
            print(f"[gate FAIL] {tool}: {e}")
            ok = False
    vbox = shutil.which("VBoxManage")
    if vbox is None and os.path.isfile(VBOX_KNOWN):
        vbox = VBOX_KNOWN
    if vbox is None:
        print("[gate SKIP] VirtualBox absent: "
              "image/test need a VM host")
    else:
        try:
            r = subprocess.run([vbox, "--version"],
                               capture_output=True, text=True,
                               timeout=30, cwd=ROOT)
            print("[gate ok] VirtualBox: " +
                  (r.stdout or "").strip())
        except Exception as e:
            print(f"[gate FAIL] VirtualBox: {e}")
            return False, False
    return ok, vbox is not None


def step(name):
    print(f"== gate: {name} ==")
    r = subprocess.run(NOVA + [name], cwd=ROOT)
    if r.returncode != 0:
        print(f"[gate FAIL] {name} -> {r.returncode}")
        return False
    print(f"[gate ok] {name}")
    return True


def main():
    ok, has_vm = env_step()
    if not ok:
        return 1
    for name in ["lint", "build"]:
        if not step(name):
            return 1
    if has_vm:
        if not step("image"):
            return 1
        print("[gate PASS] env + lint + build + image")
    else:
        print("[gate PARTIAL] env + lint + build green; "
              "image+test SKIPPED (no VM) — not a pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
