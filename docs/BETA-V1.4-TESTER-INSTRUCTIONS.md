# NOVA OS v0.8.0 — Tester Instructions (private pilot)

Follow exactly, in order. Work alone first;
ask the project owner only if blocked. Never
use a real disk or personal data.

## 1. Verify the package

Unzip `NOVA-OS-Beta-v0.8.0.zip` into a NEW empty
directory. Verify checksums with the tool on
your host:

```text
certutil -hashfile SHA256SUMS.txt SHA256   (Windows)
sha256sum -c SHA256SUMS.txt                (Linux/macOS)
```

All lines must match. Confirm MANIFEST.txt lists
exactly the files you received. If anything
fails: stop, keep the zip, report CHECKSUM-FAIL.

## 2. Create a disposable VM (VirtualBox 7.x)

BIOS/PIIX3, 2 CPUs, 1024 MB RAM, NAT virtio
network, one IDE controller, no audio/USB.
Convert and attach (exact commands are in the
packaged EXTERNAL-BETA-QUICKSTART.md §3 —
follow that document from here on for setup).

## 3. Boot and first run

Start the VM, wait ~20 s, click inside the
window to capture the keyboard (Right Ctrl
releases). Expect: banner, INIT-OK, GATES-OK,
`NOVA OS 0.8.0 init`, `> ` prompt. Run:

```text
help
about
uptime
stat
boguscmd
```

Record each result (expected outputs are in the
quickstart). `about` must show `0.8.0`/`abi=1`;
`boguscmd` must answer `unknown: try help`.

## 4. Persistence test (the headline feature)

Use a UNIQUELY named file (your initials + a
digit, e.g. `put /disk/jd1 hello`) so results
stay attributable. Then:

```text
cat /disk/jd1        (expect your exact bytes)
```

Power off CLEANLY: wait for the `> ` prompt,
then host-side `VBoxManage controlvm <vm>
poweroff`; confirm `VMState="poweroff"`. Cold
boot, then:

```text
cat /disk/jd1        (expect identical bytes)
rm /disk/jd1         (expect ok)
cat /disk/jd1        (expect no such file)
```

Repeat once on `/nova` with different content.
Distinguish in your report: download integrity /
first boot / command correctness / reboot
persistence / usability / crashes. Do NOT test
abrupt power loss or crash consistency — no rig
exists and the contract excludes it.

## 5. Report

Fill FEEDBACK-TEMPLATE.md completely: host OS,
VBox version, CPU/RAM/storage config, artifact
hash, per-test outcomes, expected vs actual,
exact steps, frequency/severity, evidence
(screenshot or serial excerpt), fresh-VM
reproduce check, answers to the 7 questionnaire
items. Redact secrets, tokens, personal data.
Submit through the channel the project owner
gave you. A missing report is NOT RUN — silence
proves nothing.
