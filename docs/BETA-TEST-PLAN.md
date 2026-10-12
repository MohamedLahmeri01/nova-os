# NOVA OS — Beta Test Plan & Results (Phase N4)

Environment: Windows 11 host, VBox 7.2.6,
NOVA-OS-CONSTRAINED (2 CPU/1024 MB, IDE,
NAT virtio). Disposable images only (harness-
managed VDIs); no host-disk tests exist or ran.

## Automated (existing framework, run this phase)

| Category | Suite | Result |
| Boot/init (incl. repeat, markers) | T5 run-vbox + 4 cold boots | PASS (~16–19 s, all markers) |
| Fault/panic reporting | T6 panic-test (in-suite ×4, standalone ×2) | PASS after fix (2 in-suite FAILs → root-caused, fixed, 3 greens) |
| Memory (alloc/free/bounds/exhaust) | T7 + PMM/VMM/HEAP/MEMMAP-OK | PASS |
| Syscall boundary (fuzz 20k, invalid ptr/len/id/op) | T8 + C/D/F batteries | PASS |
| FS (20k paths, mutants, stress, fsck) | T9–T11 + FS/FAT/EXT/NOVA + FSSTRESS | PASS |
| Scheduler (lifecycle/preempt/sleep/wake/acct/starve) | T12–T18 equiv + SCHEDW/STARVE/SLEEP/RUNTIME | PASS |
| Network (shapes, flood, churn, timeouts, pool) | T14/T21 + NETSTRESS/SOCKBLK/IRQPUMP | PASS |
| Shell (grammar, fuzz 3k+1k full-range) | T23 | PASS |
| Trace/fetch/stats | TRACE/TRACEFETCH-ADV/STATS | PASS |
| Regression total | full suite | 23/23 ×3 consecutive |
| Gates | lint + ci/gate.py | PASS |

## Manual (screenshot-evidence, this phase)

| Test | Steps | Result |
| Console boot | start headless, screenshot | PASS (banner, proofs, prompt, cursor) |
| Live diagnostics | type `stat` | PASS (ticks=3413 switches=63500 irqs=3429 threads=2 pmm_free=261746) |
| Error handling | type `cat /nope` | PASS (`cat: no such file`, prompt returns, stable) |
| Typed help + backspace | (prior session) | PASS (echo, erase clean, output correct) |
| Session end | type `exit` | PASS (guidance line, clean stop, no panic) |
| Tombstone honesty | code read (unreachable init print reverted) | PASS (guidance prints pre-exit, where it executes) |

## Not run / not applicable

- External-host ping/DNS (BLOCKED: needs NAT egress
  validation, not a code gap; loopback+gateway proven).
- Power-loss/corruption (NOT APPLICABLE: no
  durability claims made; ramfs documented volatile).
- SMP/ARM64/TCP/TLS (NOT APPLICABLE: absent by scope).
- 135 s soak (NOT RUN: prior-phase evidence stands;
  no covered path changed except the marker print).

## Classification key

PASS / FAIL / BLOCKED / NOT RUN / NOT APPLICABLE
as above. No skips converted to passes; no failure
hidden (the T6 FAILs are recorded with root cause).
