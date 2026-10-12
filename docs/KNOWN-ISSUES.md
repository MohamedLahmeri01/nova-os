# NOVA OS v0.8.0 — Known Issues (beta)

Read before reporting: these are confirmed,
documented behaviors, not new bugs.

1. No mouse pointer exists (MOUSE-ABSENT by
   design). Everything is keyboard-driven.
2. Typing does nothing until you click inside
   the VM window (VirtualBox capture). Right
   Ctrl releases. Not a guest bug.
3. Overwriting with shorter text leaves the old
   tail (`put f hello` then `put f hi` reads
   `hillo`). No truncation exists; rewrite the
   file (rm + put) for exact contents.
4. FAT names display 8.3 UPPERCASE (`v8.txt`
   lists as `V8.TXT`). Contents unaffected.
5. `exit` ends the session permanently; reset
   the VM for a new shell (it tells you so).
6. Widget commands (`winopen`, `click`, …) paint
   raw pixels over the console; run `clear`.
7. Long lines wrap mid-word at column 40.
8. Boot selftests rewrite fixed scratch files
   (`/W.TXT`, `/BIG.BIN`, `/NOVA-P.TST` cycle)
   on the data volumes every boot; user files
   are untouched.
9. External hosts, mid-write power loss, files
   near 64 KB end-to-end, and physical hardware
   are untested — see the contract. Do not file
   these as surprises; file concrete failures.
10. The ACPI Shutdown menu does nothing (no guest
    power handling); use host-side poweroff after
    the prompt returns.
11. Only the gateway answers ping; anything beyond
    the virtual /24 says `no route to host` (no
    routing by design). Public DNS names DO resolve
    (`nslookup example.com` works via the NAT
    proxy). There is no TCP/HTTP/HTTPS at all —
    do not file their absence as a bug.
