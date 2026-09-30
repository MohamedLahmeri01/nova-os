# NOVA OS — Resource Budget (normative for tests)

Status: accepted for Phase 0/1. Changes require an ADR with measurements.

## 1. Constrained baseline (the whole OS must run here)

```text
CPUs:   2
RAM:    1024 MB
VRAM:   16 MB
Chipset: PIIX3, BIOS boot, PAE on, long mode on, HPET on
NIC:    none (isolated until the net stack exists)
USB:    off   Audio: none
```

Rationale: if NOVA boots, schedules, and runs a shell in 2 CPU / 1 GB,
scaling UP later (SMP, large memory) is an optimization problem, not a
rescue operation. Scaling issues are handled by design (§4), not by
requiring bigger hardware.

Enforcement: `python tools/nova.py vbox-check` and `run-vbox` re-apply
`--cpus 2 --memory 1024` on every run via `ensure_test_vm()`. Budget
drift without an ADR is impossible; the values live in exactly one
place (`tools/nova.py`: `CONSTRAINED_CPUS`, `CONSTRAINED_RAM_MB`).

## 2. Phase budgets (targets, not claims)

| Phase | Must fit in | Notes |
|---|---|---|
| 1 boot → serial + panic | 1 GB, 2 CPU, boot < 5 s in VBox | no disk needed |
| 2 memory (PMM+VMM+heap) | +0 MB (structures scale with RAM, overhead logged) | fragmentation numbers required |
| 4 scheduler | 2 CPU fair; no starvation at 256 threads | latency/throughput checked in |
| 6 userspace (init+shell) | total guest < 64 MB resident | measured via VBox metrics |
| 7 RAMFS+FAT32 | image + cache < 256 MB | corruption tests at 1 GB |

Any phase exceeding its row blocks the next phase until an ADR either
optimizes or re-budgets with evidence.

## 3. Host-side frugality

The build driver (`nova.py`) is single-threaded, stdlib-only Python:
no parallel build fan-out, no resident daemons, artifacts only under
`build/`, `images/`, `vms/` (all gitignored). It never installs
software or starts VMs except the single `NOVA-OS-CONSTRAINED` shell.

## 4. Scaling plan (later, not now)

- SMP: single-core correctness first; bring-up behind a flag; load
  balancing measured at 2 CPU before any >2 CPU target.
- Memory pressure: reclaim architecture designed in Phase 2, stress
  tests (high process counts, page-cache pressure) run at 1 GB to
  prove behavior, then re-run at larger sizes for scaling curves.
- No phase may assume >2 CPUs or >1 GB in generic code paths.
