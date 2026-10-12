# NOVA OS v0.8.0 — External Beta Quickstart

No source code needed. About 20 minutes, most of
it waiting for boots.

## 1. Prerequisites

- 64-bit Windows/Linux/macOS with VirtualBox 7.x
  installed (6.1/7.0 untested — report if used).
- ~50 MB free (images are 1 MB each + VM overhead).
- A terminal (Windows: PowerShell or cmd).

## 2. Verify the download

In the package directory, check every file:

```text
certutil -hashfile SHA256SUMS.txt SHA256   (Windows)
sha256sum -c SHA256SUMS.txt                (Linux/macOS)
```

Every line must say OK / match. If anything
fails: stop, delete the package, re-download.
Also confirm MANIFEST.txt lists exactly the
files you received.

## 3. Create the disposable VM

```text
VBoxManage createvm --name "NOVA-OS-BETA-TEST" --register
VBoxManage modifyvm "NOVA-OS-BETA-TEST" --ostype Other \
  --cpus 2 --memory 1024 --chipset piix3 --firmware bios \
  --nic1 nat --nictype1 virtio --audio none --usb off
VBoxManage storagectl "NOVA-OS-BETA-TEST" --name IDE \
  --add ide --controller PIIX3
```

Convert and attach the four images (1 MB each):

```text
VBoxManage convertfromraw images/nova-os.raw boot.vdi --format VDI
VBoxManage convertfromraw images/fat32.raw fat32.vdi --format VDI
VBoxManage convertfromraw images/ext4.raw ext4.vdi --format VDI
VBoxManage convertfromraw images/nova.raw nova.vdi --format VDI
VBoxManage storageattach "NOVA-OS-BETA-TEST" --storagectl IDE \
  --port 0 --device 0 --type hdd --medium boot.vdi
VBoxManage storageattach "NOVA-OS-BETA-TEST" --storagectl IDE \
  --port 0 --device 1 --type hdd --medium fat32.vdi
VBoxManage storageattach "NOVA-OS-BETA-TEST" --storagectl IDE \
  --port 1 --device 0 --type hdd --medium ext4.vdi
VBoxManage storageattach "NOVA-OS-BETA-TEST" --storagectl IDE \
  --port 1 --device 1 --type hdd --medium nova.vdi
```

(If a device slot is occupied, detach it first.
Never attach a disk containing real data.)

## 4. First boot

```text
VBoxManage startvm "NOVA-OS-BETA-TEST" --type gui
```

Wait ~20 s. You should see, in order: the banner
(`NOVA OS console: keyboard only…`), `INIT-OK`,
`GATES-OK`, `NOVA OS 0.8.0 init`, spawn proofs,
then `> ` with a cursor. If the screen stays
black past 60 s, see §9.

## 5. First commands (click inside the window first!)

Without the click, your keys go to the host.
Right Ctrl gives the keyboard back.

```text
> help
commands: help echo mem ticks uptime about … !!
> about
nova sh 0.8.0 (x86-32 BIOS prototype)
abi=1
> uptime
uptime ticks=… (~… s @100Hz nominal)
> stat
ticks=… switches=… / irqs=… threads=… / pmm_free=…
> boguscmd
unknown: try help
```

## 6. Persistence demo (the headline feature)

```text
> put /disk/beta.txt hello-beta
> cat /disk/beta.txt
hello-beta
```

Shut down CLEANLY (the guest has no ACPI/power
handling, so this is a host-side procedure):
wait for the `> ` prompt to return — that proves
every write completed — then:

```text
VBoxManage controlvm "NOVA-OS-BETA-TEST" poweroff
VBoxManage showvminfo "NOVA-OS-BETA-TEST" | grep VMState
```

Proceed only at `VMState="poweroff"`. Then start
again, and:

```text
> cat /disk/beta.txt
hello-beta
> rm /disk/beta.txt
ok
```

Same shape works on `/nova`. Files under `/` or
`/test` do NOT survive reboot (ramfs, by design).
Overwriting with shorter text leaves the old tail
(no truncation — documented, not a bug).

## 7. Supported shutdown

- Clean: wait for the shell prompt (writes are
  synchronous — a returned prompt means bytes are
  on the virtual platter), then host-side
  `controlvm … poweroff`. Optionally type `exit`
  first (ends the shell with guidance). There is
  no in-guest shutdown command and no ACPI handling
  (ACPI Shutdown menu does nothing — do not use it
  as proof of anything).
- Forced reset mid-command is NOT safe for data
  and proves nothing except what the contract
  says: mid-write loss is untested territory.

## 8. Evidence collection

- Screenshot: Machine menu → Take Screenshot.
- Serial log (optional, advanced): add a COM1
  file UART before booting; the log mirrors the
  screen exactly.

## 9. Troubleshooting

- Black screen: wait 60 s; check the VM really
  started; re-create the VM if the storage
  layout differs from §3.
- Typing does nothing: click inside the window
  (capture). No mouse pointer exists — normal.
- `cat` shows wrong bytes: did a second VM share
  the image? Never share images between VMs.
- `put` says usage: empty text is refused; give
  at least one character (max ~100/line).
- Unexpected panic screen: photograph it, note
  the `TRAP`/`PANIC` lines, and file it — that
  output is the diagnostic, not a dead end.
