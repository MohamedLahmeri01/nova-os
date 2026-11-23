"""Boot the BIOS live image alone and exercise the shell over a real keyboard."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid


def smoke(vbox, image, scratch):
    def box(*args):
        subprocess.run([vbox, *args], check=True, timeout=60,
                       stdout=subprocess.DEVNULL)

    vm = "NOVA-OS-LIVE-" + uuid.uuid4().hex
    with tempfile.TemporaryDirectory(dir=Path(scratch).resolve()) as tmp:
        disk = Path(tmp, vm, "boot.vdi")
        log = disk.with_name("serial.log")

        def expect(marker, offset=0):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                data = log.read_bytes()[offset:] if log.exists() else b""
                if marker in data.replace(b"\r", b""):
                    return
                if b"PANIC" in data:
                    break
                time.sleep(0.1)
            raise RuntimeError(f"missing {marker!r}; serial: {data[-800:]!r}")

        box("createvm", "--name", vm, "--ostype", "Other", "--basefolder",
            tmp, "--register")
        running = False
        try:
            box("modifyvm", vm, "--firmware", "bios", "--cpus", "1",
                "--memory", "128", "--nic1", "none", "--boot1", "disk",
                "--uart1", "0x3f8", "4", "--uartmode1", "file", str(log))
            box("convertfromraw", image, str(disk), "--format", "VDI")
            box("storagectl", vm, "--name", "SATA", "--add", "sata")
            box("storageattach", vm, "--storagectl", "SATA", "--port", "0",
                "--device", "0", "--type", "hdd", "--medium", str(disk))
            box("startvm", vm, "--type", "headless")
            running = True
            expect(b"LIVE: RAM only")
            expect(b"\n> ")
            for command, result in [
                ("put /note livebytes", b"\n> "),
                ("cat /note", b"\nlivebytes> "),
                ("fsck", b"\nfsck failed E19"),
                ("run /bin/hi", b"HI-OK pid="),
                ("chmod 644 /bin/hi", b"\n> "),
                ("run /bin/hi", b"\nrun failed E13"),
            ]:
                offset = log.stat().st_size
                box("controlvm", vm, "keyboardputstring", command)
                box("controlvm", vm, "keyboardputscancode", "1c", "9c")
                expect(result, offset)
            box("controlvm", vm, "poweroff")
            running = False
            after = Path(tmp, "after.raw")
            box("clonemedium", "disk", str(disk), str(after), "--format", "RAW")
            box("closemedium", "disk", str(after))
            if after.read_bytes() != Path(image).read_bytes():
                raise RuntimeError("live session modified the boot disk")
        finally:
            try:
                if running:
                    box("controlvm", vm, "poweroff")
            finally:
                box("unregistervm", vm, "--delete")
    print("LIVE-OK: shell, RAM file, unavailable fsck, child, disk unchanged")


if __name__ == "__main__":
    smoke(*sys.argv[1:])
