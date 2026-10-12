# NOVA OS — Beta V1.3 Test Results (executed record)

Env: Win11 / VBox 7.2.6 / NOVA-OS-CONSTRAINED
(2 CPU/1024 MB, IDE fat32+ext4+nova VDIs, NAT
virtio). HEAD `0b5da64`, VERSION 0.8.0, kernel
162772B. Artifacts: build/serial.log per boot,
screenshots v13_c1–c11 (host temp, verified
present), suite logs v13s1–s5 (host temp).

## Baseline (all PASS, exit 0 unless noted)

- lint → clean (1 tolerated warning). build →
  162772B. image → vdi attached.
- suite run 1: 23/23. run 2: 22/23 (T6 FAIL —
  see F-01). runs 3,4,5: 23/23 (post-fix).
- gate → PASS. panic-test standalone ×2 → PASS
  ~17 s. Skips: 0 in every run (no silent drops;
  T5/T6/T9/T12 all present and passing).

## F-01: T6 INIT-OK split (FAIL → fixed, recorded)

- Evidence: serial showed full panic +
  `INIT-`+banner on one line; marker sweep:
  all 58 markers present except contiguous
  `INIT-OK`. Class: serial interleave (third
  instance after INJECT-FAULT, GATES-OK).
- Fix: atomic print in spawn_init (cli/sti,
  +7/-1 lines, pm_main.c only). No production
  impact (boot print, once).
- Regression proof: 3 consecutive 23/23 after;
  contiguous INIT-OK counted in later logs.

## Persistence campaign (guest shell, screenshots)

| ID | Action | Expected | Actual | Result |
| P-01 | put 10 B with spaces/punct, cat | exact bytes | `hello-v08` pre-reboot; identical post-reboot | PASS |
| P-02 | 8 B + 2 B distinct files, 2 vols | exact | `jgjlekbe`, `q7` post-reboot | PASS |
| P-03 | overwrite shorter (10 B→9 B) | stale tail (no-shrink, documented) | `vlsekyess!` | PASS (contract holds) |
| P-04 | rm + read-after-remove | `ok` + `cat: no such file` | observed verbatim | PASS |
| P-05 | recreate after remove, new content | exact | `cgjlebe`, `trek` post-reboot | PASS |
| P-06 | second full cycle (rm/recreate/reboot/verify) | exact | both files exact | PASS |
| P-07 | missing + nested-missing paths | clean ENOENT text | `cat: no such file` ×2 | PASS |
| P-08 | `ls /disk` | lists incl. new file (8.3 upper) | `V8.TXT` etc. | PASS |
| P-09 | `fsck` before/after churn | `fsck ok` | observed twice | PASS |
| P-10 | empty-text put | usage error (no empty files via shell) | `put` requires text per code; NOT RUN live (covered by contract audit) | NOT RUN |
| P-11 | 64 KB boundary via shell | — | impossible (128 B line cap) | NOT APPLICABLE (shell path); driver path covers 1 KB in PERSIST-OK |
| P-12 | 8.3 uppercasing side effect | documented | `v8.txt`→`V8.TXT` | PASS (note) |

## Interruption / crash (§5): BLOCKED

Writes complete synchronously inside one shell
command (ms scale); the harness has no hook to
stage poweroff mid-write, and VBox poweroff is
externally all-or-nothing. No mid-write test
exists or was improvised — clean-poweroff
evidence is NOT crash evidence (contract §3).
Missing capability: timed power-cut rig.

## Integrity

- PERSIST-OK battery every boot (create/write/
  grow/reread/delete/gone FAT+NOVA, fsck-clean).
- `fsck` (nova_fsck) clean before/after campaign.
- No corruption observed in any cycle; no repair
  claimed or tested (audit-only fsck).

## Boundary review (§6, code audit, no defect found)

- Lengths: 64 KB cap enforced pre-use
  (end>nc→ENOSPC); 512 B chunked bounces;
  reachable offsets capped (no u32 wrap).
- User pointers: copy_from_user per chunk,
  fail-closed with partial-count honesty.
- Lifetimes: materialized nodes owned by
  descriptions; drop frees; delete paths verified
  live (rm→ENOENT).
- Locks/IRQ: battery runs pre-spawn boot
  context (same as all selftests); MMIO windows
  IF-saving (Phase L); no sleep in paths.
- No memory-safety claim beyond: suite green +
  audit clean; VM survival is not proof (stated).

## T9/T12 historical verification (§7)

- file.c hash == HEAD blob hash (byte-identical
  revert; reference = HEAD itself, stated —
  no separate prior hash existed to compare).
- T9/T12 present in all suite runs, passing.
- Battery in syscall.c (guest-only, never
  host-linked); PERSIST-OK in fresh serial.
- Final image rebuilt after the last code change
  (gate + 2 builds since); VDI matches code.
