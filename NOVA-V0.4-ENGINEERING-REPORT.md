# NOVA v0.4 Engineering Report (Phases A–X, hardening + robustness)

Date: 2026-10-06. Head at write time: see log. No release tag
(S discipline: maintainer declares releases; VERSION stays 0.2.0).

## 1. Baseline

```text
commit:    1023ffa (post-v0.3 + v0.2.0 audit, clean) → work ends HEAD (see log)
compiler:  gcc mingw-w64 15.2.0 (-m32 freestanding; tests -O2/-m32)
host:      Ryzen 5 5500 / Win11 / Python 3.11.2 / VBox 7.2.6
VM:        2 CPU / 1024 MB, NAT+virtio, COM1 serial
tests:     22/22 → 23/23 (T23 new), 0 failed, 0 skipped, 0 removed, 0 weakened
```

## 2. Security

- Per-argument syscall audit (`docs/SYSCALL-ARGUMENTS.md`,
  35 calls, all pointer args classified): every payload via
  centralized copies, structs snapshotted once, NUL-scan
  exception documented, six normative invariants.
- TOCTOU follow-up: fault-escape structurally impossible
  (immutable mappings — no munmap/mprotect/paging); tearing
  bounded by single-pass snapshots (accepted semantics).
- Memory isolation: runtime grid audit (`MEMMAP-OK`) —
  null RO, text RO-supervisor, data/VRAM RW-supervisor,
  large pages RW-supervisor, gap unmapped, clone isolation
  + exact user bits on a scratch space.
- NX: CPUID-detected (`CPU-NX=1`), unenforceable without
  PAE — reported, never passed off. W^X without NX
  enforced (RO text). No RWX by intent anywhere.
- Kernel mappings: verified supervisor-only (no PTE_US on
  any kernel map); user access faults. Speculative
  channels out of scope per threat model.
- Security model formalized (`docs/SECURITY-MODEL.md`):
  trust/components/inputs/8 boundaries with validation,
  lifetime, failure, concurrency, ownership + live
  weakness register W1–W6 (incl. corrected W2).
- Remaining threats: cross-thread tearing (accepted),
  ring-0 drivers unisolated, DMA w/o IOMMU, supply chain
  (unpinned tools, no runner), speculative execution.

## 3. Scheduler

- Workloads: RR pair (overlap=1, switches=3), 6-runnable
  (switches=7), wake latency ≤2 ticks (measured 1), sleep
  ±0 ticks (asked 20, actual 20) — `SCHED-W` lines.
- Starvation: `hog_ticks=14 switches=3 early=0` (preemption
  fires, nowhere to go); three-level case argued identical
  by pick rule (no separate test: same code path).
- Switches: `g_sched_switches` single-funnel counter.
- Lifecycle: reap-and-compare count assert (no leaked/
  double-counted threads).
- Accounting re-audit: tick/wake/sleep/graveyard paths
  sound single-core; u32 wrap accepted with wrap-safe
  comparisons; no per-thread runtime yet (noted gap).
- Policy decision: STRICT PRIORITY RETAINED (ADR-0072) —
  all behavior correct-by-design, tripwire set for any
  future change.

## 4. SMP

- LOCK atomics (both arch backends), spinlock + irqsave
  pair (IF-saving), `docs/LOCKING.md` normative (derived
  nesting: 3 sites, all non-contending; sleep/scheduler/
  IRQ/refcount rules), `smp_cpu_id` + BSP record asserted.
- Contention: host T22 (atomics + contract) + guest 2×100k
  irqsave increments exact + IF-state asserts (`SYNC-OK`).
- Per-CPU: `docs/PERCPU.md` design (structure, accessor
  candidates, migration checklist, anti-patterns).
- NOT implemented: AP scheduling, per-CPU data, TLB
  shootdown, balancing. APs parked, unchanged.

## 5. Filesystem fuzzing

- Manifests (`probe offset hex expect`): FAT32 7 rows,
  EXT4 7 rows, NOVA-FS 9 rows (shared driver+fsck).
- Driver replay in T10/T11/T12 (pristine reloaded per
  row, recorded errno asserted); fsck replay migrated
  to manifest in T13.
- Findings fixed: ineffective `fat-loop` row replaced by
  `dir-loop` + index-10 listing (step-cap proof); wrong
  inode offset caught by determinism check (off-by-32).
- Semantics pinned: single-cluster reads stop at size;
  stat never loads inodes (checksum fires on read);
  bitmaps are fsck domain; copy-1 fallback saves mounts.

## 6. Network/shell fuzzing

- UDP/IP shapes (10 files: version/MF/TTL/trunc/proto/
  short/cksum/huge + accept-shape validating generator
  checksums); queue flood (6→4 ordered + drop-newest +
  empty timeout) in T21.
- Shell T23: real sh.c + libc, syscalls neutered via SDK
  seam (`NOVA_TEST_STUB_SYSCALL`, guest-identical when
  undefined), `-m32` for pointer fidelity; 16 corpus
  lines + 3000 PRNG soup + strtoi vectors. Found + fixed:
  signed-overflow UB in `nova_strtoi` (now refuses).
- Corpus: 7 dirs (dhcp/dns/udp/fat32/ext4/nova_fs/shell),
  generators in-repo, `CORPUS-OK 7/7` gate (SKIP≠PASS).

## 7. Observability

- Stats counters (SYS_STATS) + shell `stat` + dispatch
  proof (pre-existing v0.3 work, re-verified).
- Bounded event ring (256×12B, IF-safe emit): thread
  create/exit, sleep/wake, faults, diverts; panic dumps
  last 12 (proven in fault image); `TRACE-OK` selftest.
- Benchmarks: 8 PERF workloads sized past tick noise
  (ctx 4000/10, lock 4M/2, call 1M/4, + upper-bound
  zeros documented as <1 tick); `PERF-OK` on completion.
- Pending: live `trace` fetch (needs syscall design),
  sub-tick TSC calibration, per-thread runtimes.

## 8. CI

- `nova lint` (syntax/py/ADR-gaps/empties; errors fail;
  1 tolerated warning + policy doc).
- `ci/gate.py`: env versions + lint + build + image;
  VBox absence → explicit PARTIAL/SKIP (verified both
  paths). No YAML without a runner (documented refusal).
- Warnings: 3 fixed zero-risk, inventory in
  `docs/WARNINGS.md`, `-Werror` deferred to empty-file +
  host strategy decision.

## 9. Performance (before → after)

- New numbers (no prior baseline existed): see §3/§7.
  Zeros are upper bounds (<10ms), not rates.
- Heap churn ~6.4M ops/s (host, carried).
- Boot wall unchanged within noise (~11s markers).
- No optimizations claimed (hardening release).

## 10. Remaining risks (explicit)

SMP scheduling, DMA/IOMMU, speculative execution,
supply chain, advanced isolation (NX/ASLR need long
mode), driver isolation, packet-shape breadth,
concurrent-FS stress, shell syntax that doesn't exist
yet, hosted runner, `-Werror`, TSC calibration,
per-thread runtime, live trace fetch.

## 11. Tests

Old: 22/22 preserved verbatim in behavior (T13 migrated
to manifest, same 4 mutants; T14/T21 extended, nothing
removed). New: T23 (shell), corpus replays in
T10–T14/T21 (+7 replay sites), MEMMAP/TRACE/PERF/SCHEDW
gates. Total 23/23, 0 failed, 0 skipped, 0 removed,
0 weakened. Lint + gate green.

## v0.5 recommendation

Scheduler policy only with workload-demonstrated harm;
SMP scheduling behind its own ADR using LOCKING.md +
PERCPU.md; tracing fetch syscall; TSC calibration;
shell/FS breadth from the corpus pattern; `-Werror`
when WARNINGS.md empties. No new subsystems before
the P2 tail closes.
