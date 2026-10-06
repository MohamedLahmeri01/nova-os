# NOVA v0.4 Baseline Audit (read-only — no source modified)

Date: 2026-10-06. Method: git inspection + full suite run +
source re-verification of every v0.3 claim. Verdict at the end.

## 1. Tree state

```text
HEAD:     1023ffa (docs: v0.2.0 architecture audit)
status:   clean (git status --short empty)
describe: v0.2.0-11-g1023ffa (11 commits past the v0.2.0 tag)
VERSION:  0.2.0 (no v0.3 bump — correct, release undeclared)
tags:     v0.1.0, v0.2.0 (no v0.3 tag — correct)
```

## 2. Suite baseline (reproduced this session)

`python tools/nova.py test` → **22 passed, 0 failed, 0 skipped**
(T1–T22; T22 sync new in v0.3). Environment: gcc 15.2.0 /
Python 3.11.2 / VBox 7.2.6 / Ryzen 5 5500 / VM 2 CPU + 1024 MB.
Gate chain observed complete (through GATES-OK).

## 3. v0.3 report verification (claims vs source)

| v0.3 claim | Source evidence | Verdict |
|---|---|---|
| LICENSE Apache-2.0 | `LICENSE` 209 lines, full text | CONFIRMED |
| ADR-0064 ratifies 0008 | both files present, status lines flipped with history intact | CONFIRMED |
| ADR-0065 C-first, 0 .rs | repo-wide: 0 `.rs`, 0 `Cargo.toml`; rustc 1.98.1 present, unused | CONFIRMED |
| copy_from/to_user centralized | defined validate.c:66/82, declared validate.h:19-20, **8 call sites** in syscall.c (115, 562, 622, 680, 710, 1091, 1124, 1253) + stats 1293 | CONFIRMED |
| Only copy_user_path still raw | sole remaining volatile deref in syscall.c:200 | CONFIRMED |
| RO kernel text + CPU-NX print | vmm.c PT0 loop, `text_end` linker symbol, selftest -7 | CONFIRMED (VMM-OK in log) |
| AP words moved to .data | ap_tramp.S `.data` section for _ap_stack/_ap_lapic | CONFIRMED (SMP-OK in log) |
| STARVE numbers | sched.c starve test + gate; log shows hog_ticks/switches | CONFIRMED |
| g_sched_switches funnel | incremented in sched_switch_to | CONFIRMED |
| Atomics/spinlock/LOCKING.md | arch headers + kernel/sync/* + docs/LOCKING.md | CONFIRMED |
| T22 + SYNC-OK | test_sync.c, suite green | CONFIRMED |
| lint + gate.py + corpus | `nova lint` exists, ci/gate.py exists, 11 .bin files + mkcorpus.py + T14 replay | CONFIRMED |
| SYS_STATS (35) + stat + GATES-OK | syscall.h:35/35, types.h struct, sh.c `stat`, pm_main GATES-OK | CONFIRMED |
| 14 empty dirs deleted | verified absent (drivers/, libs/, fs/, net/, scripts/, examples/, userspace/{coreutils,services,shell}, kernel/{core,ipc,security,scheduler}) | CONFIRMED |
| dist carries LICENSE | nova.py docs list + T18 rows | CONFIRMED by code read (T18 green in suite) |

## 4. Source audit (v0.4 Phase A–X inputs, verified)

- **Syscalls (35, 0–34 + STATS 35):** every pointer arg validated; payloads via centralized helpers; structs snapshotted once; scalars direct. `mem_page_state` has exactly one caller (`validate_usermem`). No direct user derefs outside syscall.c except MMIO/VRAM.
- **Mappings:** kernel never sets PTE_US (grep: define + readback only); user maps always U/S with per-map RW; clones share PT0 (RO text propagates). Null RO, guard absent, WP on. No NX bit exists in this paging mode.
- **Scheduler:** strict 32-level + RR/level, 10-tick slices, no aging; BLOCKED + sleep list + tick expiry; switch counter live. 1-vs-2-CPU identical (APs park in `cli;hlt`).
- **Sync/SMP:** xchg/cmpxchg/xadd + pause in both arch headers; spinlock (trylock/EBUSY, owner diag, irqsave IF-saving); LOCKING.md normative; `smp_cpu_id` (LAPIC ID reg) + BSP record asserted in smp_boot. APs parked, unchanged.
- **FAT32:** BPB parsed, chain caps, no LFN, 1-sector cache, grow-only ≤64KB, write-through chunks, EROFS-guarded graft writes. Limits: PATH 256, NAME 32, FILE 64KB, NFD 16.
- **EXT4:** enforced geometry, direct+single-indirect, rec_len checks, EROFS writes. No journal/extents/symlinks.
- **NOVA-FS:** journaled writer, crc32c, redundant SB, fsck both sides, 4-mutant T13.
- **Corpus/fuzz:** 11 DHCP/DNS wire seeds replayed in T14 (argv[1] absolute path); logic fuzz fixed-seed PRNG (T8 20k, T14 2k, T15/16/21 2k); NO shell fuzz, NO FS-image mutants beyond T13's 4, NO packet-shape fuzz beyond crafted vectors.
- **lint/gate:** `nova lint` = syntax + py_compile + ADR 0001..0071 continuity + empty-dir scan (errors fail; 1 tolerated warning: jail defensive frame). `ci/gate.py` = lint+build+image, verified PASS. No YAML, no runner.
- **Build flags:** `-m32 -ffreestanding -nostdlib -Os -mno-sse -mno-mmx -Wall -Wextra` (no `-Werror`); mingw PE → ld scripts → objcopy binary; `foo`→`_foo` rule incl. linker symbols.
- **Arch code:** `arch/x86` live (`__i386__`); `arch/x86_64` dormant headers; no raw asm outside arch/ + a few cli/sti/hlt idioms in kernel (sched, pump paths).
- **Shell:** 128B lines, printable-only + backspace, per-command bounded parsers (e.g. 64u guard in `put`), no quotes/pipes/redirect/escaping/globbing; ~40 builtins incl. `stat`, `click`, `uloop`, `nslookup`.
- **Net bounds:** MTU 1500, RXQ 4×1600, SOCK_MAX 8, payload 1472; no TCP/IPv6 anywhere (grep clean); single waiter/EBUSY; demux never truncates.
- **v0.3 diff review (6173e88..HEAD, 57 files, +2563/-70):** all changes correspond to report phases; no unrelated edits detected in the stat; two pre-existing warnings remain (jail frame) plus fixed three.

## 5. Claims vs reality (must-fix list for v0.4)

1. v0.3 report says "zero weakened tests" — TRUE (suite 22/22, no skips).
2. Report says TOCTOU "closed" — TRUE for fault-escape (immutable mappings verified); tearing accepted explicitly. No action.
3. Report says AP scheduling "explicitly later" — TRUE (parked, untouched).
4. `docs/ABI.md` row 35 present (regen verified in suite T17).
5. One soft spot: v0.3 report claims heap "~6.4M ops/s (unchanged, host)" without a recorded pre-change number in-report — acceptable (host-only metric), note for P-phase baselines.

## 6. What must NOT change in v0.4 baseline

Boot chain, paging mode (non-PAE evidence stands), scheduler policy, disk polling (ADR-0063), AP park, syscall numbers/semantics, gate chain contents, test expectations, ABI v1 + row 35, LICENSE/ADR history, deterministic seeds.

## 7. What must change (v0.4 input queue)

SECURITY-MODEL.md (absent); per-argument syscall table (absent); mapping permission table (manuscript in ADR-0067 prose, needs the §D grid); scheduler workload suite (only hog+worker exist); lock hierarchy derivation (§H2); FS corpora (FAT32/EXT4/NOVA-FS — only wire codecs exist); shell corpus (absent); trace buffer (absent); warning ratchet (1 tolerated → 0 + policy); hosted CI (gate.py exists, runner absent).

## Verdict

```text
BASELINE VERIFIED
```

Tree clean at 1023ffa, 22/22 reproduced, every v0.3 claim
verified in source or logs, no silent modifications found.
Cleared for v0.4 Phase A implementation.
