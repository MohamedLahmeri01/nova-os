# Guest integration tests

`tools/nova.py test` runs the diagnostic boots (T5/T6) and the live
session (`live_boot.py`, T24). T24 creates its own temporary BIOS VM
with one CPU, 128 MiB, no NIC and a single SATA disk. It types into the
shell, reads back a RAM file, runs a child and compares the powered-off
disk's contents with the input image. It then removes its VM.

To run just T24 after `python tools/nova.py build`:

```sh
python tests/integration/live_boot.py VBoxManage images/nova-os-live.raw build
```
