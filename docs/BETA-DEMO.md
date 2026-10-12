# NOVA OS — Beta Demonstration (Phase N5, executed)

Reproducible developer demo, performed against the
working tree (no commit). Each step ran for real;
screenshots live in the session record.

## Procedure

1. Build: `python tools/nova.py build`
   Expect: `stage1=500B kernel=162068B sectors=317`
   (+ userspace blobs embedded).
2. Attach: `python tools/nova.py image`
   Expect: raw→vdi attached to IDE P0D0, serial
   routed to `build/serial.log`.
3. Boot: start `NOVA-OS-CONSTRAINED` headless
   (or GUI: click inside the window to capture).
   Expect: ~16–19 s to console banner:
   `NOVA OS console: keyboard only, no mouse…`,
   `INIT-OK`, `GATES-OK` (serial), spawn proofs,
   `nova sh` prompt with cursor — on screen.
4. Diagnostics: type `stat`
   Expect: `ticks=… switches=…` /
   `irqs=… threads=…` / `pmm_free=…` (live).
4b. New in N.2 (executed, screenshot-proven):
   `help` (37-name list), `help cat` (desc + usage),
   `uptime` (`~N s @100Hz nominal`), `about`
   (version/target/`abi=1`), `!!` (repeats `about`
   byte-identically).
5. Files: type `ls`, then `cat /nope`
   Expect: listing; then `cat: no such file`
   and a fresh prompt (system stable).
6. Safe error: type `boguscmd`
   Expect: `unknown: try help`.
7. Session end: type `exit`
   Expect: `bye; reset the VM for a new shell`,
   then stillness (no prompt, no panic).
8. Automated proof: `python tools/nova.py test`
   Expect: `23 passed, 0 failed, 0 skipped`.
9. Limitations speech (say out loud): x86-32 VBox
   only; no SMP/crypto/TCP/OTA/mouse; ramfs is
   volatile; `exit` needs a VM reset. Nothing here
   is production.

## Troubleshooting

- Blank screen after boot: still booting (wait 20 s);
  check `build/serial.log` for markers.
- Typing does nothing: click inside the VM window
  (capture); Right Ctrl releases.
- Mouse captured/invisible: expected (no driver);
  everything here is keyboard-driven.
- `exit` froze the prompt: expected — reset the VM.
- Suite T6 FAIL with full panic in log: check the
  serial for a SPLIT `INJECT…-FAULT` (fixed this
  phase; report any recurrence with the log).
