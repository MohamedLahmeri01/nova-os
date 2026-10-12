# NOVA OS v0.8.0 — Installation and First Boot

## 1. What you need

- 64-bit Windows/Linux/macOS, VirtualBox 7.x,
  ~50 MB free. No admin rights needed beyond
  normal VirtualBox use.

## 2. Verify the package (do this first)

```text
certutil -hashfile SHA256SUMS.txt SHA256   (Windows)
sha256sum -c SHA256SUMS.txt                (Linux/macOS)
```

Every line must match. Confirm MANIFEST.txt lists
exactly your files. On any failure: stop, delete,
re-download. Never boot an unverified image.

## 3. Create the disposable VM

```text
VBoxManage createvm --name "NOVA-OS-TEST" --register
VBoxManage modifyvm "NOVA-OS-TEST" --ostype Other \
  --cpus 2 --memory 1024 --chipset piix3 \
  --firmware bios --nic1 nat --nictype1 virtio \
  --audio none --usb off
VBoxManage storagectl "NOVA-OS-TEST" --name IDE \
  --add ide --controller PIIX3
```

## 4. Convert and attach the images

```text
VBoxManage convertfromraw images/nova-os.raw boot.vdi --format VDI
VBoxManage convertfromraw images/fat32.raw fat32.vdi --format VDI
VBoxManage convertfromraw images/ext4.raw ext4.vdi --format VDI
VBoxManage convertfromraw images/nova.raw nova.vdi --format VDI
VBoxManage storageattach "NOVA-OS-TEST" --storagectl IDE \
  --port 0 --device 0 --type hdd --medium boot.vdi
VBoxManage storageattach "NOVA-OS-TEST" --storagectl IDE \
  --port 0 --device 1 --type hdd --medium fat32.vdi
VBoxManage storageattach "NOVA-OS-TEST" --storagectl IDE \
  --port 1 --device 0 --type hdd --medium ext4.vdi
VBoxManage storageattach "NOVA-OS-TEST" --storagectl IDE \
  --port 1 --device 1 --type hdd --medium nova.vdi
```

If a slot reports occupied, detach it first.
Never attach anything containing real data.

## 5. First boot (what success looks like)

```text
VBoxManage startvm "NOVA-OS-TEST" --type gui
```

Within ~20 s you must see, in order: the banner
(`NOVA OS console: keyboard only…`), `INIT-OK`,
`GATES-OK`, `NOVA OS 0.8.0 init`, spawn proofs,
then `> ` with an underline cursor. Past 60 s of
black: see TROUBLESHOOTING.md.

## 6. First commands

Click inside the window (capture), then type:

```text
help
about
uptime
```

`about` must show `0.8.0`/`abi=1`. `boguscmd`
must answer `unknown: try help`. You now have a
working NOVA OS. Continue with USER-GUIDE.md
(persistence demo) or BASIC-FUNCTIONALITY-
TESTS.md (full checklist).
