#!/usr/bin/env python3
"""nova.py - NOVA OS build/test driver (Phase 0 stub).

Policy: read-only toward the host. All outputs stay inside the repo
(build/, images/, vms/). Never installs software, never changes
PATH/registry, never modifies existing VirtualBox VMs. Test VMs always
use NOVA-OS-* names on isolated networking.

Resource budget (enforced): the test target is CONSTRAINED — 2 CPUs,
1024 MB RAM — so scaling problems surface early, not after the OS
grows. run-vbox and vbox-check re-apply and verify this budget on
every invocation.

Commands: env | build | test | run-vbox | vbox-check | image | clean
"""
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
IMAGES = os.path.join(ROOT, "images")
VBOX = r"F:\VirutalBox\VBoxManage.exe"

# Constrained test budget — the whole OS must run here (see
# docs/RESOURCE-BUDGET.md). Raise only via ADR with measurements.
TEST_VM = "NOVA-OS-CONSTRAINED"
CONSTRAINED_CPUS = 2
CONSTRAINED_RAM_MB = 1024
VM_BASEFOLDER = os.path.join(ROOT, "vms")

REQUIRED_DIRS = ["kernel", "boot", "userspace", "libs", "fs", "net",
                 "drivers", "tools", "tests", "docs", "docs/adr", "ci"]
REQUIRED_DOCS = ["docs/ARCHITECTURE.md", "docs/ENVIRONMENT.md",
                 "docs/ABI-STRATEGY.md", "docs/THREAT-MODEL.md",
                 "docs/TESTING-STRATEGY.md", "docs/RESOURCE-BUDGET.md",
                 "docs/TOOLCHAIN.md", "ROADMAP.md", "README.md"]

# Phase 1 boot-test contract: the guest must print this on COM1.
MARKER = b"NOVA-OS-BOOT-MARKER"
IMAGE_RAW = os.path.join(IMAGES, "nova-os.raw")
IMAGE_VDI = os.path.join(IMAGES, "nova-os.vdi")
FAULT_RAW = os.path.join(IMAGES, "nova-os-fault.raw")
FAULT_VDI = os.path.join(IMAGES, "nova-os-fault.vdi")
SERIAL_LOG = os.path.join(BUILD, "serial.log")
CFLAGS32 = ["-m32", "-ffreestanding", "-nostdlib", "-nostartfiles",
            "-fno-pic", "-fno-pie", "-fno-asynchronous-unwind-tables",
            "-fno-omit-frame-pointer",
            "-fno-builtin", "-Wall", "-Wextra", "-Os"]

# Phase 2a allocator choice (see docs/adr/ADR-0010-allocator.md).
PMM_BACKEND_C = "kernel/mm/pmm_bitmap.c"
PMM_BACKEND_O = "pmm_bitmap.o"


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def vbox(*args, timeout=60):
    """Run VBoxManage against the TEST_VM only. Never touches other VMs."""
    return run([VBOX] + list(args), timeout=timeout)


def vminfo():
    r = vbox("showvminfo", TEST_VM, "--machinereadable")
    info = {}
    for line in (r.stdout or "").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            info[k.strip()] = v.strip().strip('"')
    return info


def ensure_test_vm():
    """Create the test VM if missing, then (re-)apply the resource budget.

    Returns (ok: bool, message: str). Enforcement is idempotent: every
    run-vbox/vbox-check call re-asserts cpus/memory, so budget drift is
    impossible without editing CONSTRAINED_* above (which needs an ADR).
    """
    if not os.path.exists(VBOX):
        return False, "VBoxManage not found"
    os.makedirs(VM_BASEFOLDER, exist_ok=True)
    r = vbox("list", "vms")
    if TEST_VM not in (r.stdout or ""):
        r = vbox("createvm", "--name", TEST_VM, "--ostype", "Other_64",
                 "--register", "--basefolder", VM_BASEFOLDER)
        if r.returncode != 0:
            return False, f"createvm failed: {(r.stderr or '')[:500]}"
    r = vbox("modifyvm", TEST_VM,
             "--cpus", str(CONSTRAINED_CPUS),
             "--memory", str(CONSTRAINED_RAM_MB),
             "--vram", "16", "--chipset", "piix3", "--firmware", "bios",
             "--pae", "on", "--longmode", "on", "--hpet", "on",
             "--rtcuseutc", "on", "--nic1", "none",
             "--audio-driver", "none", "--usb", "off",
             "--boot1", "disk", "--boot2", "none",
             "--boot3", "none", "--boot4", "none")
    if r.returncode != 0:
        return False, f"budget enforcement failed: {(r.stderr or '')[:500]}"
    info = vminfo()
    try:
        ok = (int(info.get("cpus", -1)) == CONSTRAINED_CPUS
              and int(info.get("memory", -1)) == CONSTRAINED_RAM_MB)
    except ValueError:
        ok = False
    if not ok:
        return False, f"budget mismatch: cpus={info.get('cpus')} memory={info.get('memory')}"
    return True, (f"VM '{TEST_VM}' state={info.get('VMState')} "
                  f"cpus={info.get('cpus')} memory={info.get('memory')}MB")


def cmd_env():
    """Read-only host capability check. Returns 0 if Phase-1-ready-ish."""
    print("== NOVA OS env (read-only) ==")
    ok = True
    for tool, argv in [("git", ["git", "--version"]),
                       ("gcc", ["gcc", "--version"]),
                       ("python", [sys.executable, "--version"]),
                       ("cmake", ["cmake", "--version"]),
                       ("rustc", ["rustc", "--version"])]:
        try:
            r = run(argv, timeout=30)
            print(f"[ok] {tool}: {r.stdout.strip().splitlines()[0] if r.stdout else 'present'}")
        except Exception as e:
            print(f"[missing] {tool}: {e}")
            ok = False
    if os.path.exists(VBOX):
        r = run([VBOX, "--version"], timeout=30)
        print(f"[ok] VirtualBox: {r.stdout.strip()}")
    else:
        print("[missing] VirtualBox VBoxManage not found")
        ok = False
    # read-only probes that write ONLY to temp-equivalent inside repo
    print("[info] QEMU qemu-system-x86_64: not required in Phase 0/1 (VirtualBox target)")
    print("[info] nasm/grub/xorriso: checked at Phase 1 image step, not here")
    return 0 if ok else 1


def cmd_build():
    """Validate skeleton + freestanding compile probe (output in build/)."""
    print("== nova build (Phase 0 stub) ==")
    missing = [d for d in REQUIRED_DIRS if not os.path.isdir(os.path.join(ROOT, d))]
    missing += [f for f in REQUIRED_DOCS if not os.path.isfile(os.path.join(ROOT, f))]
    if missing:
        print(f"[fail] missing skeleton entries: {missing}")
        return 1
    print("[ok] skeleton present")
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(ROOT, "kernel", "entry.c")
    out = os.path.join(BUILD, "entry_probe.o")
    if not os.path.isfile(src):
        print("[fail] kernel/entry.c missing")
        return 1
    try:
        r = run(["gcc", "-ffreestanding", "-nostdlib", "-c", src, "-o", out], timeout=60)
        if r.returncode == 0 and os.path.exists(out):
            print(f"[ok] freestanding compile probe passed -> {out}")
        else:
            print(f"[blocked] gcc probe failed:\n{r.stderr[:2000]}")
            return 2  # blocked, not failed-by-design
    except FileNotFoundError:
        print("[blocked] gcc not on PATH; toolchain setup is manual (see docs/ENVIRONMENT.md)")
        return 2
    rc, res = build_phase1()
    if rc != 0:
        print(f"[fail] phase1 build: {res}")
        return 1
    print(f"[ok] stage1={res['stage1_bytes']}B kernel={res['kernel_bytes']}B "
          f"sectors={res['kernel_sectors']}")
    return 0


def build_phase1(fault=False):
    """Compile+link stage1 and stage2 to flat binaries. Returns (rc, info|msg).

    fault=True compiles pm_main.c with -DNOVA_FAULT_TEST (deliberate-panic
    path for `nova panic-test`; outputs kernel_fault.pe/bin).
    Toolchain: mingw gcc -m32 (PE objects) -> ld with single-section
    scripts -> objcopy -O binary. Layout assertions fail the build loudly
    instead of producing a silently broken image (see docs/TOOLCHAIN.md).
    """
    os.makedirs(BUILD, exist_ok=True)
    pm_obj = "pm_main_fault.o" if fault else "pm_main.o"
    pe = "kernel_fault.pe" if fault else "kernel.pe"
    kb_name = "kernel_fault.bin" if fault else "kernel.bin"
    defs = ["-DNOVA_FAULT_TEST"] if fault else []
    for src, obj, extra_defs in [("boot/stage1.S", "stage1.o", []),
                                 ("boot/stage2asm.S", "stage2asm.o", []),
                                 ("kernel/pm_main.c", pm_obj, defs),
                                 ("kernel/serial.c", "serial.o", []),
                                 ("kernel/panic.c", "panic.o", []),
                                 ("kernel/mm/pmm.c", "pmm.o", []),
                                 ("kernel/mm/vmm.c", "vmm.o", []),
                                 ("kernel/mm/heap.c", "heap.o", []),
                                 ("kernel/mm/addrspace.c", "addrspace.o", []),
                                 ("kernel/irq/idt.S", "idt.o", []),
                                 ("kernel/irq/irq.c", "irq.o", []),
                                 ("kernel/irq/lapic.c", "lapic.o", []),
                                 ("kernel/time/pit.c", "pit.o", []),
                                 ("kernel/syscall/entry.S", "syscall_entry.o", []),
                                 ("kernel/syscall/syscall.c", "syscall.o", []),
                                 ("kernel/syscall/validate.c", "validate.o", []),
                                 ("kernel/ctx.S", "ctx.o", []),
                                 ("kernel/user.S", "user_asm.o", []),
                                 ("kernel/user.c", "user.o", []),
                                 ("kernel/gdt.c", "gdt.o", []),
                                 ("kernel/smp.c", "smp.o", []),
                                 ("boot/ap_tramp.S", "ap_tramp.o", []),
                                 ("kernel/thread/thread.c", "thread.o", []),
                                 ("kernel/sched/sched.c", "sched.o", []),
                                 ("kernel/process/process.c", "process.o", []),
                                 (PMM_BACKEND_C, PMM_BACKEND_O, [])]:
        s = os.path.join(ROOT, src)
        if not os.path.isfile(s):
            return 1, f"missing {src}"
        extra = []
        if src.endswith(".c"):
            extra = ["-I", os.path.join(ROOT, "kernel"),
                     "-I", os.path.join(ROOT, "kernel", "mm")] + extra_defs
        elif src.endswith(".S"):
            extra = ["-I", os.path.join(ROOT, "boot")]
        r = run(["gcc"] + CFLAGS32 + extra + ["-c", s, "-o",
                 os.path.join(BUILD, obj)], timeout=60)
        if r.returncode != 0:
            label = " (fault)" if fault else ""
            return 1, f"compile failed {src}{label}:\n{(r.stderr or '')[:2000]}"
    for objs_in, ld, pe_out in [
            (["stage1.o"], "boot/stage1.ld", "stage1.pe"),
            (["stage2asm.o", pm_obj, "serial.o", "panic.o",
              "pmm.o", "vmm.o", "heap.o", "addrspace.o", "idt.o", "irq.o",
              "lapic.o", "pit.o", "syscall_entry.o", "syscall.o",
              "validate.o", "ctx.o", "user_asm.o", "user.o", "gdt.o",
              "smp.o", "ap_tramp.o", "thread.o", "sched.o", "process.o",
              PMM_BACKEND_O],
             "boot/stage2.ld", pe)]:
        cmd = (["gcc"] + CFLAGS32 +
               ["-Wl,-T," + os.path.join(ROOT, ld)] +
               [os.path.join(BUILD, o) for o in objs_in] +
               ["-o", os.path.join(BUILD, pe_out)])
        r = run(cmd, timeout=60)
        if r.returncode != 0:
            return 1, f"link failed {pe_out}:\n{(r.stderr or '')[:2000]}"
    for pe_in, binary in [("stage1.pe", "stage1.bin"),
                          (pe, kb_name)]:
        # .text+.datax: both load sections, LMA order, gap padded
        # (see boot/stage2.ld). .bss is NOLOAD (zeroed at entry).
        # Single call preserves layout; -j drops PE .reloc/debug.
        r = run(["objcopy", "-j", ".text", "-j", ".datax", "-O", "binary",
                 os.path.join(BUILD, pe_in),
                 os.path.join(BUILD, binary)], timeout=60)
        if r.returncode != 0:
            return 1, f"objcopy failed {pe_in}: {(r.stderr or '')[:500]}"
    s1 = os.path.join(BUILD, "stage1.bin")
    kb = os.path.join(BUILD, kb_name)
    s1n, kbn = os.path.getsize(s1), os.path.getsize(kb)
    if s1n > 510:
        return 1, f"stage1 too big: {s1n} bytes (>510, must fit boot sector)"
    with open(s1, "rb") as f:
        s1d = f.read()
    with open(kb, "rb") as f:
        kbd = f.read()
    if b"NVSC" not in s1d:
        return 1, "stage1.bin missing NVSC sector-count patch point"
    if MARKER not in kbd:
        return 1, f"{kb_name} missing boot marker string"
    # stage2 prologue: xorl %ebp (31 ED), pushl $0x2 (6A 02), popfl (9D),
    # cld (FC), movl $bss_start,%edi (BF ...). See boot/stage2asm.S.
    if not kbd.startswith(b"\x31\xed\x6a\x02\x9d\xfc\xbf"):
        return 1, f"{kb_name} stage2 prologue wrong (layout broken)"
    sectors = (kbn + 511) // 512
    if sectors > 127:
        return 1, f"kernel too big for single INT13 load: {sectors} sectors"
    return 0, {"stage1_bytes": s1n, "kernel_bytes": kbn,
               "kernel_sectors": sectors}


def cmd_test():
    """Host tests T1-T4 plus VBox-gated boot test T5 (with SKIP semantics)."""
    print("== nova test ==")
    passed, failed, skipped = 0, 0, 0
    # T1: skeleton dirs exist
    if all(os.path.isdir(os.path.join(ROOT, d)) for d in REQUIRED_DIRS):
        print("[pass] T1 skeleton directories present"); passed += 1
    else:
        print("[fail] T1 skeleton directories missing"); failed += 1
    # T2: boot-info header version macro present
    hdr = os.path.join(ROOT, "boot", "boot_info.h")
    try:
        with open(hdr) as f:
            body = f.read()
        if "NOVA_BOOT_INFO_VERSION" in body:
            print("[pass] T2 boot_info.h versioned"); passed += 1
        else:
            print("[fail] T2 boot_info.h has no version macro"); failed += 1
    except FileNotFoundError:
        print("[fail] T2 boot/boot_info.h missing"); failed += 1
    # T3: kernel entry exposes documented symbol
    src = os.path.join(ROOT, "kernel", "entry.c")
    try:
        with open(src) as f:
            body = f.read()
        if "nova_entry" in body and "NOVA-OS-BOOT-MARKER" in body:
            print("[pass] T3 kernel entry + boot marker present"); passed += 1
        else:
            print("[fail] T3 kernel entry symbol/marker missing"); failed += 1
    except FileNotFoundError:
        print("[fail] T3 kernel/entry.c missing"); failed += 1
    # T4: static image checks (SKIP when no image built yet)
    if os.path.isfile(IMAGE_RAW):
        with open(IMAGE_RAW, "rb") as f:
            raw = f.read()
        t4ok = (len(raw) % 512 == 0 and raw[510:512] == b"\x55\xAA"
                and b"NVSC" in raw[:512])
        if t4ok:
            print(f"[pass] T4 image {len(raw)}B, boot signature ok"); passed += 1
        else:
            print("[fail] T4 image signature/layout broken"); failed += 1
    else:
        print("[skip] T4 no image yet (run `nova image` first)"); skipped += 1
    # T5: VBox-gated boot test (SKIP when no image or no VBox)
    if not os.path.exists(VBOX):
        print("[skip] T5 VirtualBox unavailable"); skipped += 1
    elif not os.path.exists(IMAGE_VDI):
        print("[skip] T5 no VDI yet (run `nova image` first)"); skipped += 1
    else:
        print("[info] T5 booting constrained VM (~40s)...")
        try:
            r = subprocess.run([sys.executable,
                                os.path.join(ROOT, "tools", "nova.py"),
                                "run-vbox"],
                               capture_output=True, text=True, timeout=150)
        except subprocess.TimeoutExpired:
            print("[fail] T5 run-vbox timed out"); failed += 1
            r = None
        if r is not None:
            if r.returncode == 0:
                print("[pass] T5 guest printed boot marker"); passed += 1
            elif r.returncode == 2:
                print("[skip] T5 blocked (image not attached)"); skipped += 1
            else:
                print(f"[fail] T5 guest did not boot:\n{r.stdout[-800:]}")
                failed += 1
    # T6: VBox-gated panic test (SKIP when no VBox)
    if not os.path.exists(VBOX):
        print("[skip] T6 VirtualBox unavailable"); skipped += 1
    else:
        print("[info] T6 booting fault image in constrained VM (~60s)...")
        try:
            r = subprocess.run([sys.executable,
                                os.path.join(ROOT, "tools", "nova.py"),
                                "panic-test"],
                               capture_output=True, text=True, timeout=240)
        except subprocess.TimeoutExpired:
            print("[fail] T6 panic-test timed out"); failed += 1
            r = None
        if r is not None:
            if r.returncode == 0:
                print("[pass] T6 guest printed panic diagnostics"); passed += 1
            else:
                print(f"[fail] T6 panic path broken:\n{r.stdout[-800:]}")
                failed += 1
    # T7: host heap test (heap.c + real PMM on a fake arena)
    print("[info] T7 compiling + running host heap test...")
    os.makedirs(BUILD, exist_ok=True)
    heap_exe = os.path.join(BUILD, "test_heap.exe")
    cmd = ["gcc", "-O2", "-Wall", "-Wextra",
           "-I", os.path.join(ROOT, "kernel"),
           "-I", os.path.join(ROOT, "kernel", "mm"),
           "-I", ROOT,
           os.path.join(ROOT, "tests", "unit", "test_heap.c"),
           os.path.join(ROOT, "kernel", "mm", "heap.c"),
           os.path.join(ROOT, "kernel", "mm", "pmm.c"),
           os.path.join(ROOT, "kernel", "mm", "pmm_bitmap.c"),
           "-o", heap_exe]
    r = run(cmd, timeout=120)
    if r.returncode != 0:
        print(f"[fail] T7 heap test compile:\n{(r.stderr or '')[:1500]}")
        failed += 1
    else:
        r = run([heap_exe], timeout=120)
        print((r.stdout or "") + (r.stderr or ""))
        if r.returncode == 0:
            print("[pass] T7 host heap test"); passed += 1
        else:
            print("[fail] T7 host heap test"); failed += 1
    # T8: host syscall-validation fuzz (validate.c + fake page map)
    print("[info] T8 compiling + running validation fuzz...")
    sys_exe = os.path.join(BUILD, "test_syscall.exe")
    cmd = ["gcc", "-O2", "-Wall", "-Wextra",
           "-I", os.path.join(ROOT, "kernel"),
           os.path.join(ROOT, "tests", "unit", "test_syscall.c"),
           os.path.join(ROOT, "kernel", "syscall", "validate.c"),
           "-o", sys_exe]
    r = run(cmd, timeout=120)
    if r.returncode != 0:
        print(f"[fail] T8 fuzz compile:\n{(r.stderr or '')[:1500]}")
        failed += 1
    else:
        r = run([sys_exe], timeout=120)
        print((r.stdout or "") + (r.stderr or ""))
        if r.returncode == 0:
            print("[pass] T8 validation fuzz"); passed += 1
        else:
            print("[fail] T8 validation fuzz"); failed += 1
    print(f"== {passed} passed, {failed} failed, {skipped} skipped ==")
    return 0 if failed == 0 else 1


def _assemble_and_attach(kernel_bin_name, raw_path, vdi_path):
    """Assemble raw image from stage1.bin + kernel bin, VDI-convert, attach.

    Returns (rc, msg). Operates on the TEST_VM only (idempotent)."""
    with open(os.path.join(BUILD, "stage1.bin"), "rb") as f:
        s1 = bytearray(f.read())
    with open(os.path.join(BUILD, kernel_bin_name), "rb") as f:
        kb = f.read()
    sectors = (len(kb) + 511) // 512
    if sectors > 127:
        return 1, f"kernel too big for single INT13 load: {sectors} sectors"
    idx = bytes(s1).find(b"NVSC")
    s1[idx + 4:idx + 6] = sectors.to_bytes(2, "little")
    s1 += b"\x00" * (510 - len(s1)) + b"\x55\xAA"
    kpad = kb + b"\x00" * ((512 - len(kb) % 512) % 512)
    os.makedirs(IMAGES, exist_ok=True)
    raw = bytes(s1) + kpad
    # VDI requires >= 1MB source; pad (dynamic VDI keeps host cost ~KBs).
    if len(raw) < 1024 * 1024:
        raw += b"\x00" * (1024 * 1024 - len(raw))
    with open(raw_path, "wb") as f:
        f.write(raw)
    vbox("storageattach", TEST_VM, "--storagectl", "IDE",
         "--port", "0", "--device", "0", "--medium", "none")
    vbox("closemedium", "disk", vdi_path, "--delete")
    if os.path.exists(vdi_path):
        os.remove(vdi_path)
    r = vbox("convertfromraw", raw_path, vdi_path, "--format", "VDI")
    if r.returncode != 0:
        return 1, f"convertfromraw: {(r.stderr or '')[:800]}"
    vbox("storagectl", TEST_VM, "--name", "IDE", "--remove")
    r = vbox("storagectl", TEST_VM, "--name", "IDE", "--add", "ide",
             "--controller", "PIIX4")
    if r.returncode != 0:
        return 1, f"storagectl: {(r.stderr or '')[:500]}"
    r = vbox("storageattach", TEST_VM, "--storagectl", "IDE",
             "--port", "0", "--device", "0", "--type", "hdd",
             "--medium", vdi_path)
    if r.returncode != 0:
        return 1, f"storageattach: {(r.stderr or '')[:500]}"
    r = vbox("modifyvm", TEST_VM, "--uart1", "0x3F8", "4",
             "--uartmode1", "file", SERIAL_LOG)
    if r.returncode != 0:
        return 1, f"uart config: {(r.stderr or '')[:500]}"
    return 0, f"{raw_path} ({os.path.getsize(raw_path)}B) -> {vdi_path}"


def _boot_expect(markers, shot_name, timeout_s=30):
    """Boot TEST_VM headless; pass iff ALL markers appear on serial.

    Returns (ok, elapsed, tail). Always screenshots + powers off."""
    if os.path.exists(SERIAL_LOG):
        os.remove(SERIAL_LOG)
    r = vbox("startvm", TEST_VM, "--type", "headless", timeout=60)
    if r.returncode != 0:
        return False, 0, f"startvm failed: {(r.stderr or '')[:800]}"
    data, elapsed = b"", 0
    for _ in range(timeout_s):
        time.sleep(1)
        elapsed += 1
        try:
            with open(SERIAL_LOG, "rb") as f:
                data = f.read()
        except OSError:
            data = b""
        if all(m in data for m in markers):
            break
    try:
        vbox("controlvm", TEST_VM, "screenshotpng",
             os.path.join(BUILD, shot_name))
    except Exception:
        pass
    vbox("controlvm", TEST_VM, "poweroff")
    for _ in range(15):
        time.sleep(1)
        if vminfo().get("VMState") == "poweroff":
            break
    ok = all(m in data for m in markers)
    tail = data.decode("ascii", "replace")[-800:] if data else "(no serial log captured)"
    return ok, elapsed, tail


def cmd_run_vbox():
    print("== nova run-vbox ==")
    ok, msg = ensure_test_vm()
    print(f"[{'ok' if ok else 'fail'}] {msg}")
    if not ok:
        return 1
    info = vminfo()
    attached = any(str(v).endswith("nova-os.vdi") for v in info.values())
    if not os.path.exists(IMAGE_VDI) or not attached:
        print("[blocked] No image attached. Run `python tools/nova.py image` first.")
        return 2
    ok, elapsed, tail = _boot_expect([MARKER, b"PMM-OK", b"VMM-OK",
                                       b"HEAP-OK", b"IDT-OK", b"TIMER-OK",
                                       b"SCHED-OK", b"USER-OK",
                                       b"USER-SCHED-OK", b"SYSCALL-OK",
                                       b"SMP-OK"],
                                       "vbox-boot-proof.png")
    if ok:
        print(f"[PASS] full init markers on serial after ~{elapsed}s "
              f"(2 CPU / 1024 MB)")
        return 0
    print(f"[FAIL] boot markers incomplete within 30s. serial tail:\n{tail}")
    return 1


def cmd_vbox_check():
    """Verify the constrained budget without starting anything."""
    print("== nova vbox-check ==")
    ok, msg = ensure_test_vm()
    print(f"[{'pass' if ok else 'fail'}] {msg}")
    info = vminfo() if ok else {}
    for key in ["ostype", "chipset", "firmware", "pae", "longmode",
                "nic1", "usb", "VMState"]:
        if key in info:
            print(f"  {key}={info[key]}")
    r = vbox("list", "vms")
    others = [l for l in (r.stdout or "").splitlines() if "NOVA-OS-" not in l]
    print(f"[info] {len(others)} non-NOVA VM(s) present, untouched:")
    for l in others:
        print(f"  {l.strip()}")
    return 0 if ok else 1


def cmd_image():
    print("== nova image ==")
    ok, msg = ensure_test_vm()
    print(f"[{'ok' if ok else 'fail'}] {msg}")
    if not ok:
        return 1
    rc, res = build_phase1()
    if rc != 0:
        print(f"[fail] {res}")
        return 1
    print(f"[ok] stage1={res['stage1_bytes']}B kernel={res['kernel_bytes']}B "
          f"sectors={res['kernel_sectors']}")
    rc, msg = _assemble_and_attach("kernel.bin", IMAGE_RAW, IMAGE_VDI)
    if rc != 0:
        print(f"[fail] {msg}")
        return 1
    print(f"[ok] {msg} attached to '{TEST_VM}' (IDE P0D0), "
          f"serial -> {SERIAL_LOG}")
    return 0


PANIC_MARKERS = [b"INJECT-FAULT", b"TRAP vec=6", b"trap-UD", b"PANIC",
                 b"STACK:", b"END-PANIC-HALT", b"PMM-OK", b"VMM-OK",
                 b"HEAP-OK", b"IDT-OK", b"TIMER-OK", b"SCHED-OK",
                 b"USER-OK", b"USER-SCHED-OK", b"SYSCALL-OK", b"SMP-OK"]


def cmd_panic_test():
    """Build the fault-injection image, boot it, verify panic diagnostics."""
    print("== nova panic-test ==")
    ok, msg = ensure_test_vm()
    print(f"[{'ok' if ok else 'fail'}] {msg}")
    if not ok:
        return 1
    rc, res = build_phase1(fault=True)
    if rc != 0:
        print(f"[fail] {res}")
        return 1
    print(f"[ok] fault kernel={res['kernel_bytes']}B "
          f"sectors={res['kernel_sectors']}")
    rc, msg = _assemble_and_attach("kernel_fault.bin", FAULT_RAW, FAULT_VDI)
    if rc != 0:
        print(f"[fail] {msg}")
        return 1
    print(f"[ok] {msg}")
    ok, elapsed, tail = _boot_expect(PANIC_MARKERS, "vbox-panic-proof.png")
    # Restore the normal image attachment for subsequent run-vbox/T5 runs.
    if os.path.exists(IMAGE_VDI):
        vbox("storageattach", TEST_VM, "--storagectl", "IDE",
             "--port", "0", "--device", "0", "--type", "hdd",
             "--medium", IMAGE_VDI)
    if ok:
        print(f"[PASS] panic diagnostics on serial after ~{elapsed}s "
              f"(2 CPU / 1024 MB)")
        return 0
    print(f"[FAIL] panic output incomplete within 30s. serial tail:\n{tail}")
    return 1


def cmd_bench_pmm():
    """Compile + run the bitmap vs buddy shootout on the host (Phase 2a)."""
    print("== nova bench-pmm ==")
    results = {}
    for name, src in [("bitmap", "kernel/mm/pmm_bitmap.c"),
                      ("buddy", "kernel/mm/pmm_buddy.c")]:
        s = os.path.join(ROOT, src)
        if not os.path.isfile(s):
            print(f"[fail] missing {src}")
            return 1
        exe = os.path.join(BUILD, f"bench_{name}.exe")
        os.makedirs(BUILD, exist_ok=True)
        cmd = ["gcc", "-O2", "-Wall", "-Wextra",
               "-I", os.path.join(ROOT, "kernel"),
               "-I", os.path.join(ROOT, "kernel", "mm"),
               os.path.join(ROOT, "benchmarks", "pmm", "bench.c"),
               s, "-o", exe]
        r = run(cmd, timeout=120)
        if r.returncode != 0:
            print(f"[fail] bench compile {name}:\n{(r.stderr or '')[:1500]}")
            return 1
        print(f"--- {name} ---")
        r = run([exe], timeout=120)
        print((r.stdout or "") + (r.stderr or ""))
        if r.returncode != 0:
            print(f"[fail] {name} invariants violated")
            return 1
        results[name] = r.stdout or ""
    print("== shootout complete (see docs/adr/ADR-0010-allocator.md) ==")
    return 0


def cmd_clean():
    for d in [BUILD]:
        if os.path.isdir(d):
            shutil.rmtree(d)
            print(f"[ok] removed {d}")
    print("[ok] clean (images/ artifacts are gitignored; kept by default)")
    return 0


def main(argv):
    cmds = {"env": cmd_env, "build": cmd_build, "test": cmd_test,
            "run-vbox": cmd_run_vbox, "vbox-check": cmd_vbox_check,
            "image": cmd_image, "panic-test": cmd_panic_test,
            "bench-pmm": cmd_bench_pmm, "clean": cmd_clean}
    if len(argv) != 2 or argv[1] not in cmds:
        print(f"usage: {sys.argv[0]} {{{'|'.join(cmds)}}}")
        return 2
    return cmds[argv[1]]()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
