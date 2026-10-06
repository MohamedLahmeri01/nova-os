# NOVA OS v0.3 — Hardening Report (Phases A–G)

Date: 2026-10-06. Baseline: `6173e88`, 21/21. Head: see log.
Environment: gcc mingw-w64 15.2.0 / Python 3.11.2 / VBox 7.2.6 /
Ryzen 5 5500 / VM 2 CPU + 1024 MB. No code changes outside the
phases below; no TCP/ELF/GPU/packages/ARM64/RISC-V touched.

## Baseline → final

- Baseline reproduced first: 21/21 green at `6173e88` (clean).
- Final: 22/22 green (T22 new). Gates added: STARVE-OK,
  SYNC-OK, STATS-OK, GATES-OK, KBDBLK-OK (pre-existing work,
  first gated run here for the new ones). No test removed,
  none weakened.

## Changes by phase (commits atomic, pushed)

- **A (legal):** tree-wide third-party audit (zero hits:
  no vendored code, no shipped fonts, fixtures generated
  in-repo) → `LICENSE` (Apache-2.0, ADR-0008's provisional
  pick) → ADR-0064 ratification, ADR-0008 superseded
  (history preserved) → README, dist manifest + T18 rows.
- **A.2 (language):** rustc present-but-unused verified,
  0 `.rs` files → ADR-0065 supersedes ADR-0002 (C-first +
  compensating controls; Rust components ADR-gated).
  ADR-0002 status flipped with history intact.
- **B (TOCTOU):** all user-pointer touches inventoried
  (syscall.c only; page-table walks only in
  validate_usermem) → `copy_from_user`/`copy_to_user`
  centralized in validate.c → 8 call sites converted
  (print/read/write×2/name/sock×2/wintitle) → T8 refusal
  vectors → ADR-0066 with the full copy table. Finding
  softened honestly: immutable mappings (no
  munmap/mprotect/paging) make fault-escape structural-
  ly impossible; residual is torn-data within snapshots.
  Found-and-fixed en route: static sock bounce buffers
  (cross-thread interleave), whole-print cli span.
- **C (NX/W^X):** RO kernel text via PT0 + linker
  `text_end` (mingw underscore rule applied) → caught a
  real bug first boot (AP handoff words in .text → PF;
  moved to .data) → CPUID NX report (`CPU-NX=1`,
  unenforceable without PAE, logged not pretended) →
  mapping audit (supervisor-only kernel maps verified;
  ASLR declined: no entropy + deterministic tests) →
  ADR-0067. vmm_selftest -7 asserts RO text.
- **D (scheduler):** switch counter + starvation tripwire
  (`STARVE hog_ticks=14 switches=3 early=0 done=1` —
  preemption fires, rescheduling has nowhere to go) +
  `last_used` audit (sound under IF-discipline; no
  change) + stale RR comments fixed → ADR-0068.
  1-vs-2-CPU identical by construction (APs parked).
- **E (SMP):** LOCK atomics both arch backends, IF-saving
  irqsave pair, `struct spinlock` in filled `kernel/sync/`
  (trylock/EBUSY, owner diagnostics, no-reentrant) →
  `docs/LOCKING.md` normative table → `smp_cpu_id` +
  BSP record asserted in smp_boot → host T22 (atomics +
  contract) + guest contention (2×100k exact, SYNC-OK) →
  ADR-0069. APs parked, unchanged.
- **F (hygiene):** 14 empty dirs deleted (3 more found by
  lint), `tests/integration/README`, `REQUIRED_DIRS`
  updated (skeleton check caught the deletion within
  minutes) → `nova lint` (syntax/py/ADR-gaps/empties;
  errors fail, warnings reported) → 3 warnings fixed
  (virtio sign, irq cast, smp dupes; 1 tolerated:
  jail defensive frame) → `ci/gate.py` verified
  (lint+build+image, no VM) → `tests/corpus` (11 DHCP/
  DNS seeds, `tools/mkcorpus.py`, T14 replay) →
  ADR-0070.
- **G (observability):** `SYS_STATS` (35) read-only
  counters + SDK + shell `stat` + dispatch proof via
  lent-CR3 scratch addrspace (`STATS-OK`) → `GATES-OK`
  formal summary (post-spawn-proofs placement) →
  ADR-0071.

## Security fixes (closed)

1. TOCTOU model closed (fault-escape impossible by
   mapping immutability; tearing bounded by snapshots).
2. Static bounce race + print-cli span fixed.
3. RO text enforced (AP-handoff PF found + fixed).
4. Waiter discipline already proven; re-verified green.
5. Remaining open (not closed): cross-*mapping* TOCTOU
   n/a (no remap API); speculative side channels (out
   of scope, threat model); DMA/IOMMU (documented);
   supply chain (unpinned tools, no CI runner yet).

## Benchmarks (before → after)

- Scheduler: no baseline existed → now `hog_ticks=14,
  switches=3` for 300M/100k strict-prio pair (tripwire,
  not a target).
- Heap churn ~6.4M ops/s (unchanged, host).
- Boot wall ~10–13 s markers (unchanged within noise).
- No other numbers moved (hardening release; Phase G
  counters enable future measurement).

## Tests

Full `nova test`: 21 → 22 (T22 sync). New vectors: T8
copy refusals, T14 corpus replay (11 seeds). New gates:
STARVE/SYNC/STATS/GATES-OK (+KBDBLK-OK carried). Lint
clean (1 tolerated warning). Gate script green. Zero
failures, zero skips, zero weakened tests.

## ADRs (created/superseded)

Created: 0064 (license), 0065 (language), 0066 (copies),
0067 (W^X), 0068 (scheduler), 0069 (SMP locking),
0070 (hygiene/CI/fuzz), 0071 (observability).
Superseded (history intact): 0002, 0008.
Total: 71 entries, continuity verified by `nova lint`.

## Remaining vulnerabilities / debt

P0-3 → closed as modeled above (invariant recorded in
ADR-0066; paging work re-audits). P1-1 → foundations
landed (locks, policy, per-CPU design; no AP scheduling).
P1-2 → measured (tripwire set; no policy change).
P1-3 → narrowed (RO text, isolation verified; NX/ASLR
still impossible/declined — documented). P1-5 → audited
sound (re-audit at AP scheduling). P2 → lint/CI/corpus/
scaffolds done; `-Werror`, clang-format, hosted CI,
shell/packet fuzz still open. License holder line is
change-on-request. No VERSION bump, no tag: release
declaration belongs to the maintainer (all §33 gates
except the declaration itself are met).

## Known limitations (carried + new)

Mouse dormant, EXT4 read-only, no TCP/IPv6/signals/IPC/
ELF, disk polling (ADR-0063 stands), APs parked, wire-
to-socket peerless, fault image panics by design.
New: `stat` shell surface ungated (builtin standing);
corpus covers wire codecs only.

## v0.4 recommendation

Per §36 order: TOCTOU follow-ups only if paging lands;
scheduler policy only with workload numbers; SMP
scheduling behind its own ADR using LOCKING.md §checklist;
tracing/profiling atop the stats counters; FS/shell
fuzz breadth from the corpus pattern. No new subsystems
before the P2 tail (`-Werror`, hosted CI) closes.
