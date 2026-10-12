# NOVA OS v0.8.0 — Basic Functionality Tests

Numbered, repeatable, with real outcomes from the
qualification campaign (screenshots net1–net6,
v13_c*, v8_demo* on file). Status words: PASS /
FAIL / UNSUPPORTED (documented absent, not a
defect) / BLOCKED / NOT RUN / INCONCLUSIVE.

## Package and setup

- T01 checksum verification — PASS (all lines
  match; re-verified this phase).
- T02 fresh VM setup per guide — PASS (clean
  `NOVA-OS-BETA-TEST` created, documented
  commands work verbatim).
- T03 first boot — PASS (~20 s, banner/INIT-OK/
  GATES-OK/prompt observed).

## Shell

- T04 prompt + echo typing — PASS.
- T05 `help` lists 37 commands — PASS.
- T06 `help cat` desc + usage — PASS.
- T07 `about` (0.8.0/target/abi=1) — PASS.
- T08 `uptime` (ticks + nominal seconds) — PASS
  (`ticks=6897 (~68 s …)` observed).
- T09 `boguscmd` → unknown — PASS.
- T10 malformed input (`sptcbe`, `ff`, `abot`) →
  clean unknown, session stable — PASS.
- T11 `exit` → guidance line, clean stop — PASS.

## Files

- T12 `ls /disk` lists incl. new file — PASS.
- T13 put + cat round trip — PASS (exact bytes).
- T14 overwrite shorter keeps tail (no-shrink,
  documented) — PASS (`vlsekyess!` observed).
- T15 rm + read-after-remove — PASS (`ok`,
  then `cat: no such file`).
- T16 recreate after remove — PASS.
- T17 missing/nested-missing paths — PASS.
- T18 `fsck` clean before/after churn — PASS.

## Reboot persistence

- T19 single file write→poweroff→boot→read
  identical — PASS.
- T20 multi-file, two volumes, distinct contents
  — PASS (`jgjlekbe`, `q7`).
- T21 delete/recreate cycle + reboot + verify —
  PASS (`cgjlebe`, `trek`).

## Network

- T22 gateway ping — PASS (`reply seq=1 time=0`).
- T23 off-subnet ping refuses — PASS
  (`no route to host`, by design).
- T24 public DNS via NAT proxy — PASS
  (`example.com has 104.20.23.154`).
- T25 TCP/HTTP/HTTPS — UNSUPPORTED (no code;
  grep-verified; not a defect).
- T26 mid-write power loss — BLOCKED (no rig).

## Panic/recovery (harness only)

- T27 deliberate-fault panic with classification
  + trace — PASS (established harness; T6 green).

## Unsupported (not defects)

SMP, mouse, USB, TCP/TLS/crypto/OTA/secure
boot, Linux ABI, pipes/history/completion, power
management, GPU/sound, physical hardware, files
near 64 KB end-to-end, external hosts beyond
DNS, crash consistency.
