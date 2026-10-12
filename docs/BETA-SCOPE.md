# NOVA OS — Beta Scope (Phase N1, from source inspection)

## A. Demonstrable now (verified this phase)

- Boot to console: banner, spawn proofs, shell
  prompt, cursor — screenshot-proven on display.
- 37 shell entries (34 builtins + uptime/about/!!),
  every one dispatching to a real implementation
  (table<->dispatch host-proven; no decorative entries).
- Diagnostics: `ticks`, `mem` (frames total/free),
  `stat` (ticks/switches/irqs/threads/pmm_free),
  `rt [tid]` — all live values, demo-proven.
- Files: ls/cat/mkdir/put/rm/rmdir/fsck/run
  against ramfs + grafts; errno texts on failure
  (`cat: no such file`, `failed E<num>`).
- Network: ip/ping/dhcp/uloop/nslookup over
  virtio/NAT (loopback + gateway proven; external
  hosts depend on NAT ICMP/DNS egress, untested
  this phase — see limits).
- Errors: unknown commands, bad args (usage
  lines), permission denials, clean tombstones
  (`exit` guidance, panic screens with trace).
- Repeatability: 23/23 suite + gate, cold boots
  ~16–19 s, panic path with tick equality.

## B. Implemented this phase (2 tracks, both green)

1. Deterministic fault-test marker (boot/
   diagnostics): atomic INJECT-FAULT print;
   T6 gate stable (3 consecutive 23/23).
2. Session-end guidance (usability): `exit`
   prints recovery instruction before the
   thread dies; tombstone no longer silent.
   (Respawn rejected: would leak unreaped
   processes; documented instead.)

## C. Known limitations (verified, not assumed)

- x86-32 BIOS/VBox only; BSP-only, no SMP safety.
- No crypto/TLS/secure boot/signed updates/OTA.
- UDP-only network; no TCP/IPv6/reassembly.
- ramfs evaporates on reboot; grafts read-only;
  no journaling; no HW power-loss testing.
- No Linux ABI; 9-function libc; no
  pipes/shm/signals/mmap/fork/history/completion.
- No mouse; widget commands scribble the console
  (`clear` recovers); 40-col wrap splits words.
- `exit` ends the session (reset the VM).
- External ping/DNS beyond the gateway untested.
- Soak evidence is 135 s (prior phase); longer
  runs not repeated here.

## D. Explicitly out of scope (deferred, not denied)

Desktop apps, Linux ABI, production TLS, secure
boot, OTA/rollback, SMP, USB stack, driver
expansion, FS migration, ARM64/RISC-V ports,
formal verification, any OS-replacement claim.
TCP integration: evaluated, not needed for this
beta's demo (UDP + diagnostics carry it) — stays
a roadmap item with its boundary preconditions.
