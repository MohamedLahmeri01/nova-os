# NOVA v0.5 Phase L Report — Integration & Release Readiness

## 1. Executive Summary

Phase L audited Phases A–K as one system and found
two real IF-discipline defects (both blind-sti class,
both P1 release blockers), fixed them minimally,
proved the fixes with two new batteries, corrected
five stale documentation surfaces, and re-ran the
entire campaign green. Verdict: **B — CONDITIONALLY
RELEASE READY** (A withheld only for the standing
accepted limitations, all documented; zero blockers
remain). One commit, not pushed.

## 2. Starting Commit

`434be3e` (Phase K). Verified: HEAD SHA
`434be3e65f3abb7c3a19bf786b162dc7c3fd5dd0`,
`git status` clean, `git diff --check` clean.

## 3. Baseline

At `434be3e` before any change: lint clean (1
tolerated warning) + ADR continuity 0001..0080;
`test` 23/23 + CORPUS-OK 7/7; guest boot PASS
with all 50 OK markers incl. TRACEFETCH-ADV-OK,
RUNTIME-OK, FSSTRESS-OK, NETSTRESS-OK, GATES-OK;
panic-test PASS with trap tick=1484 == panic
ticks=1484; main image rebuilt + rebooted;
`ci/gate.py` PASS.

## 4. Release Contract

| Subsystem | Invariant | Evidence | Status |
| boot | ordered init, GATES-OK iff all selftests pass | INIT-OK + GATES-OK, 3 cold boots | HOLD |
| memory | centralized validate + copies, immutable maps | T8 vectors, MEMMAP/VMM/PMM-OK | HOLD |
| interrupts | save/restore IF, no blind sti | §10 audit + 2 fixes + batteries | FIXED→HOLD |
| exceptions | vec+tick+class, PF-only emission, panic halts | panic-test 1193==1193 | HOLD |
| scheduler | strict-prio, RR, 10-tick, 100Hz | SCHEDW/STARVE/SLEEP-OK | HOLD |
| processes | pid counter, ticks_used=sum, no reaping | RUNTIME-OK proc=140 sum=140 | HOLD |
| syscalls | 0..37 stable, validation-first, errno | ABI batteries, T-series | HOLD |
| trace | seq orders, 256x16B, projected fetch | TRACEFETCH(-ADV)-OK | HOLD |
| timebase | PIT authoritative, TSC deferred | TIMEBASE.md, 99.37Hz | HOLD |
| filesystem | VFS big lock, fsck clean | FSSTRESS-OK | HOLD |
| networking | bounds-first, 8 socks, no reassembly | NETSTRESS-OK + corpus | HOLD |
| shell | 128B, stateless lines, E<num> texts | T23 + fuzz | HOLD |
| diagnostics | correlated trap/panic/shell/contract | tick proofs + DIAGNOSTICS.md | HOLD |
| security | validation-first fail-closed, serial-only dumps | MODEL + sweeps | HOLD |

## 5. Boot/Initialization

Order audited in pm_main.c: PMM→VMM→heap→drivers
(ATA/PCI/FB/COMP/WID/mouse/virtio)→fs→grafts→net→
caps→rlimits→perm→jail→win→sock→sched→blocks→
sync→fsstress→trace→stats→fetch→bench→THREX→XSUB→
user→ticklimit→smp→demo→spawn→GATES-OK. timer_init
panics timer-dead unless 20 ticks arrive (no silent
timer failure). IF enabled by timer_init post-PIT-
program, not before. 3 cold boots + 1 soak boot,
all INIT-OK→GATES-OK, zero ordering changes in L.

## 6. Memory Integration

PMM↔VMM↔heap↔addrspace↔validate/copy↔syscalls↔fs↔
net↔trace re-verified by static grep (38 copy/
validate sites, all through validate_usermem;
zero strcpy/sprintf/atoi in kernel; 3 memcpy-class
hits are bounded hand rolls in ramfs) plus T8
refusal vectors (NULL/kernel/overflow/RO/unmapped)
and RLIM_FRAMES ceiling. No subsystem bypasses
the model. No change in L (nothing to change).

## 7. Syscall ABI

Numbers 0..37 frozen (`SYS_MAX 37u` = max number,
38 calls — ABI.md wording verified correct, the
audit's off-by-one worry unfounded). 36/37
re-batteried in D/F and re-run green in L.
Exact return/errno semantics unchanged; structs
unchanged (nova_trace_rec 20B, nova_runtime).
No ABI change in L.

## 8. User/Kernel Boundary

Path user→validate→copy→op→copy-back holds at all
38 sites. TOCTOU: snapshots + load-bearing
invariant (ADR-0066), re-grepped clean. New L code
(XSUB) runs kernel-side only (internal APIs, no
user pointers) — adds no boundary surface.
THREX touches no user memory.

## 9. Interrupts/Exceptions

Full sti/cli audit (38 sti sites): 36 paired or
boot/init-legitimate (pit bring-up, pm_main idle,
selftest CR3 windows, park-pattern exits,
check-park pairs). 2 defects found (see §30):
mm_exit blind sti (virtio_net.c) and
sched_wakeup blind sti (sched.c), both reachable
from IF=0 (rx_lock-held pump/demux; kbd ISR).
sched_can_sleep hardened same-class (no IF=0
caller today). Post-fix: holders truly run IF=0;
ISR never re-enables mid-handler.

## 10. Scheduler

Policy untouched (strict-prio + RR + 10-tick +
100Hz verified in serial: SCHED/SCHEDW/STARVE/
SLEEP-OK). wakeup/can_sleep changes are IF-
discipline only (list ops identical). RR pair in
XSUB exercises competition; THREX exercises
create/exit churn (15-burst). Starvation tripwire
still trips correctly (STARVE-OK).

## 11. Timebase

PIT authoritative re-confirmed (TIMEBASE.md:
11931→100.0094Hz, measured 99.37Hz). g_ticks =
delivered-IRQ counter, never wall clock: grep
shows all consumers tick-native + wrap-safe
(<2^31 comparisons). Sleep/RX/sock/DHCP/
watchdog/rlimit/slice classes all tick-based.
No change in L.

## 12. Trace

Ring + seq + fetch + panic-tail all re-run green
(TRACE/TRACEFETCH/TRACEFETCH-ADV-OK). Ordering
authority = seq (re-asserted in XSUB via
trace_count cap check ≤256). PF-only emission
unchanged. No change in L.

## 13. Runtime/Scheduler Correlation

XSUB asserts sched_thread_stat monotonicity
(rt1 ≥ rt0) across the 40-round mix; RUNTIME-OK
(proc=140 sum=140) re-run green; query200 cost
still 0 ticks. THREX create/exit feeds
TRACE_CREATE/EXIT + runtime freeze-at-exit paths.
Coherent story: create→run→sleep/wake→accumulate→
exit→persist, all observed in one boot.

## 14. Filesystem

FSSTRESS-OK re-run green (big lock + fsck clean).
XSUB adds 40 rounds of create→write→read-verify→
alloc→fd→drop→unlink interleaved with net/sched
load: byte-exact every round, refs balanced
(drop frees desc), unlink at refs==0. No deadlock
(IF discipline now real under pump-adjacent
paths), no corruption, no leaked refs (THREX-style
count check N/A — fd table returns to empty by
construction: alloc+drop per round). VFS big lock
kept (no fine-grained attempt per §14 rule).

## 15. Networking

NETSTRESS-OK re-run green (flood/churn/late/
rebind/pair + 2000-frame demux). XSUB adds 80
sock open/close cycles (2 threads × 40) + live
pump drains inside the mix: pool never exceeds 2
of 8, no EMFILE, no stale delivery, queue stays
bounded (drop-newest). RXQ=4, pool=8, MTU 1472
bounds untouched. No reassembly (limitation,
kept). The mm fix repairs the drill's "IF=0
throughout" claim (previously false past the
first window) — drill + IRQPUMP-OK re-run green.

## 16. Shell

T23 re-run green (20 corpus + 3000 fuzz + 1000
full-range, VERDICT correct). K errno texts
unchanged. No grammar change. Shell+fs+net+trace
+runtime combination happens every boot (batteries
run pre-spawn) plus XSUB's kernel-side mix;
host fuzz covers parser breadth. Privilege gates
re-verified via CAPS/PERM/JAIL-OK.

## 17. Diagnostics

K contract re-proven post-fix: panic-test UD shows
TRAP tick=1193 == panic ticks=1193 + tid/pid.
Fault→trap→panic→tick→trace chain intact after
IF fixes (fixes touch only enable-discipline, not
evidence paths). THREX/XSUB print machine-
greppable OK lines (burst=/rounds=). No leaks of
pointers/regs/credentials (serial-only boundary
re-audited, §28).

## 18. Cross-Subsystem Scenarios

A (cpu+net flood+trace): NETSTRESS under tick
preemption + trace emitting — green.
B (fs+sched+fetch): FSSTRESS + TRACEFETCH in one
boot under preemption — green.
C (shell+fs+runtime): T23 dispatch + RUNTIME-OK +
FS-OK — green.
D (sock churn+sleep/wakeup+timeout): SOCKBLK-OK +
NETSTRESS re-run — green.
E (fault+fetch+diag): panic-test + tick equality —
PASS (1193==1193).
F (fs+net+shell+trace+runtime): new XSUB-OK (40
rounds, RR pair, byte-exact) — green.
40/40 rounds × deterministic seed patterns.

## 19. Resource Exhaustion

| Resource | Cap | cap-1 | cap | cap+1 | Recovery | Proof |
| threads | 16 | 15 ok | 16th ok | 17th refuses 0 | reap→create ok | THREX-OK burst=15 |
| fds | 16 | ok | ok | EMFILE | drop→alloc | RLIM clamp-full + fd-reuse |
| socks | 8 | ok | 8th ok | EMFILE | close→open | sock_block + sock.c:435 |
| RX queue | 4 | ok | full | drop-newest | drain→accept | queue flood |
| TX | 1 frame | ok | EAGAIN | EAGAIN | consume→send | TX tests |
| trace | 256 | ok | wraps | seq-gap loss | fetch ok | 600-emit ADV |
| heap/pages | arena | — | ENOMEM/ENOSPC | — | free→alloc | RLIM_FRAMES, reap frees |
| shell in/out | 128B/4KB | ok | truncate | truncate | next line clean | T23 + full-range |
| processes | heap-bound | — | no cap | — | n/a (no reap) | ACCEPTED (OOM §29) |
| fd table | 16 | XSUB ±0 | — | — | per-round drop | XSUB-OK |

All bounded failures refuse without corruption.

## 20. Long-Run Stability

Soak: manual headless boot, 135 s wall (~13.5k
ticks @ ~100 Hz) running shell-idle, then clean
poweroff. Serial: THREX/XSUB/GATES-OK present, no
FAIL, no PANIC, no fault. Measured exactly: crash/
hang/fault-free over ~13.5k ticks + clean
poweroff + clean next boot. NOT measured (no
in-guest post-boot growth metric exists):
heap/PMM/fd-leak deltas across the soak — stated
as limitation, not claimed leak-free. Repeated
alloc/free paths (THREX pool math, XSUB fd ±0,
reap frees stacks) bound the leak surface by
construction.

## 21. Recovery

EAGAIN→retry→success (rx_wait/pump loops);
timeout→retry (ETIMEDOUT paths); invalid fd→
valid fd (EBADF then reuse, fd-reuse test);
EMFILE→close→allocate (sock + RLIM proofs);
fs error→valid op (errno then fsck-clean);
parse error→valid command (T23 corpus continues
past bad lines); trace overflow→fetch (seq-gap
proof + refetch identity); faulted path→halt is
BY DESIGN (no recovery — faulted process does
not continue; system halts with evidence).
No poisoning observed across any transition.

## 22. Reboot/Cold Boot

3 harness cold boots (16–19 s each) + 1 manual
soak boot + poweroff cycle: every boot INIT-OK→
GATES-OK, no stale-state dependence (ring seq
monotonic, no disk persistence of runtime
state; ramfs reseeded per boot). Fault VDI from
panic-test detached by main re-image every time
(verified: post-panic rebuild + reboot PASS).

## 23. Build Reproducibility

Clean-tree build from 434be3e + post-change
builds byte-plausible (kernel 161428B→161972B,
+544B: two batteries + IF-discipline code;
sectors 316→317). No host files, no creds, no
env paths in tree (TODO/FIXME/XXX/TEST_ONLY:
zero hits kernel/userspace/tests).
`git diff --check` clean. Artifacts stay in
build//images//vms//dist/ (untracked or ignored).

## 24. Static Security Sweep

memcpy/memmove/strncpy: 3 hits, all bounded
ramfs hand rolls (name cap + size/off bounds).
strcpy/sprintf/atoi: zero. cli/sti: 38 sites,
all audited (§9; 2 fixed). Length arithmetic:
u32 with explicit wrap guards (end<off checks).
Signed overflow: none found (Phase J removed
the shell one; kernel uses u32). Kernel pointer
exposure: serial-only by policy; user fetch
projects (re-verified). Raw CR2: PF class only.
No new regression; 2 pre-existing discipline
bugs fixed and proven.

## 25. Fuzz Regression

All campaigns re-run, corpus untouched (no seed
modified/removed): shell 3000+1000 full-range
(T23), wire shapes + 2000-frame demux flood
(NETSTRESS), FS mutants (7+7+9 driver+fsck),
queue flood, trace ADV (600-emit loss), syscall
arg batteries (C/D/F). Zero failures. XSUB adds
40 deterministic cross-subsystem rounds (new
coverage, not replacement).

## 26. ABI Compatibility

Numbers/structs/sizes/offsets/SDK mirrors
unchanged (L adds no syscall, no field, no
reorder). Shell-visible interfaces identical
(texts only grew E<num> in K, kept). Trace 20B
+ runtime struct sizes proven by ADV/F exact-
bytes batteries. Compile-time asserts: absent
(pre-existing style — batteries are the asserts;
noted, not added: no gratuitous churn in L).

## 27. Documentation Consistency

5 stale surfaces found and fixed in-commit:
LOCKING.md (sock row → g_sock_lock; ordering →
rx→sock; derived nesting +1; ISR pump wording);
SECURITY-MODEL.md (FS concurrency → big lock;
net concurrency → pool lock; W4 counts → 13+15/
20+3k+1k; W7 DEFERRED → CLOSED; packet-breadth
note dated); net.h (ISR-pump comments → ack-
only reality); virtio_net.c (rx_lock comment);
DIAGNOSTICS §12 (+IF-saving note). Cross-checked
TRACE/TIMEBASE/FS-CONCURRENCY/NET-ROBUSTNESS/
SHELL-ROBUSTNESS/ABI/ADRs 76–80: no other
contradictions. Tick-count proofs remain timing-
sensitive by nature (documented as ranges).

## 28. Security Classification

| Area | Status | Residual risk |
| user/kernel boundary | HOLD | W1 tearing semantics (accepted) |
| memory isolation | HOLD | no NX/ASLR (W2, accepted) |
| NX/W^X | ABSENT | documented, accepted |
| syscall validation | HOLD | none new |
| scheduler | HOLD | no inheritance (accepted) |
| filesystem | HOLD | coarse lock (accepted) |
| networking | HOLD | no quotas/fw/reassembly (accepted) |
| trace | HOLD | fuzzy stats (by design) |
| diagnostics | HOLD | serial physical-console trust |
| shell | HOLD | minimal grammar (accepted) |
| DMA/IOMMU | ABSENT | W5, VBox-only threat |
| SMP | ABSENT | APs parked (ADR-0069) |
| speculative exec | ABSENT | documented, accepted |
| supply chain | HOLD | stdlib-only py, pinned mingw |

No claimed-but-missing protection. W3 (ring-0
drivers) stays OPEN and explicit.

## 29. Known Limitations

x86-32 guest focus; APs parked/no SMP; no IOMMU;
PIC arch; TSC deferred/PIT authoritative; global
VFS lock; no IP frag/reassembly; bounded net
(8/4/1472); minimal shell; no ASLR/NX; 256-event
trace retention; single-executor FS; no process
reaping; no fault recovery; no double-fault
handler; fuzzy stats; no wall clock; no packet
tracing; OOM injection unavailable; no record/
replay; IRQ work charged to preempted thread.
All documented, bounded, understood, acceptable
for v0.5 — none reclassified as defects.

## 30. Bugs Discovered

L1 (P1 BLOCKER): mm_exit blind sti —
dropped IF=0 under rx_lock-held pump drains +
mid-ISR; broke drill's "IF=0 throughout".
Reachable (virtio ISR proven live: NET-OK
irq-counted). No corruption observed (ISR
acks-only) but interrupt-state corruption
occurred by definition. FIXED.
L2 (P1 BLOCKER): sched_wakeup blind sti —
called from kbd ISR + pump demux at IF=0.
Same class, same consequence shape. FIXED
(+can_sleep hardened, no IF=0 caller today).
D1–D5 (DOCUMENTATION GAP): LOCKING.md ×3,
SECURITY-MODEL.md (FS/net/W4/W7), net.h,
rx_lock comment. FIXED.
T1 (TEST GAP): thread-pool cap unproven.
CLOSED by THREX-OK.
No P0. No new fails introduced.

## 31. Bugs Fixed

L1: mm_enter takes `uint32_t *was_on`, mm_exit
takes `(cur, was_on)` — 12 call sites converted
(init/queue/notify/TX/RX/IRQ). Entry state
preserved on every path (early-EIO exits
included). No caller relied on the sti side
effect (verified: init continues sequentially; selftests run IF=1 — behavior
identical there).
L2: sched_wakeup + sched_can_sleep save/restore
IF (pushfl/cli/conditional-sti, no new includes).
List-op atomicity unchanged; park discipline
(sleep_enter IF=0 in / IF=1 out) untouched.
D1–D5: doc corrections listed in §27.
T1: THREX-OK (burst=15 proves 1+15=16 cap,
refusal, drain, recovery, no leak).
Fix policy followed: minimized, root-caused,
regression-covered (XSUB exercises both fixed
paths every round: pump MMIO windows + demux
wakeups under preemption), full suite re-run,
docs updated, recorded here. No refactoring.

## 32. Performance

PERF lines post-fix vs Phase K: ctx 6 (was 6),
heap 0, lock 1 (was 1), sock 0, fs 0, copy 0,
trace 0, thread 0, call 0 (was 1). call 1→0 is
sub-tick noise (1M dispatches < 1 tick either
way). Boot 16–19 s (was 16–19 s). Kernel +544B
(new proofs + discipline code — expected,
instrumentation, not regression). No hot-path
delta: fault paths only (mm window: 2 insns on
MMIO setup — negligible vs port I/O).

## 33. Final Test Matrix

| Test | Result | Evidence |
| 23/23 baseline | PASS | == 23 passed, 0 failed |
| CORPUS-OK 7/7 | PASS | replay phases green |
| GATES-OK | PASS | serial, 4 boots |
| TRACEFETCH-OK | PASS | serial |
| TRACEFETCH-ADV-OK | PASS | serial |
| RUNTIME-OK | PASS | proc=140 sum=140 |
| FSSTRESS-OK | PASS | serial + fsck |
| PERF-OK | PASS | lines above |
| LINT-OK | PASS | 1 tolerated warning |
| GATE-OK | PASS | ci/gate.py |
| K diagnostics | PASS | tick 1193==1193 |
| THREX-OK | PASS | burst=15, recovery |
| XSUB-OK | PASS | rounds=40 byte-exact |
| syscall ABI battery | PASS | C/D/F re-run |
| user-memory battery | PASS | T8 + sweeps |
| interrupt battery | PASS | §9 audit + fixes |
| scheduler battery | PASS | SCHEDW/STARVE/SLEEP |
| timebase battery | PASS | §11 revalidated |
| fs integration | PASS | FSSTRESS + XSUB |
| net integration | PASS | NETSTRESS + XSUB |
| shell integration | PASS | T23 + fuzz |
| x-subsystem A–F | PASS | §18 mapping |
| exhaustion matrix | PASS | §19 table |
| 10k+ tick soak | PASS | 135 s, ~13.5k ticks |
| recovery | PASS | §21 transitions |
| reboot/cold-boot | PASS | 4 boots + poweroff |
| fuzz regression | PASS | §25, corpus intact |
| static sweep | PASS | §24, 2 fixed |
| docs consistency | PASS | §27, 5 fixed |
| reproducibility | PASS | §23, diff clean |

## 34. Release Blockers

None remaining. L1/L2 were blockers on discovery
and are fixed + proven + re-regressed. Zero P0.
Zero open P1.

## 35. Release Readiness Decision

**B — CONDITIONALLY RELEASE READY.** All
mandatory tests pass, no P0/P1 remains, security
contracts hold, integration evidence is coherent
(THREX burst=15 exact math; XSUB 40/40 byte-
exact; 1193==1193 post-fix), docs match reality.
A is withheld solely because the standing
accepted limitations (§29: no SMP/IOMMU/NX/
recovery/reaping/heap-growth-metric) mean v0.5
ships as a constrained-VM release — exactly what
it documents itself as. Condition: run only in
the validated configuration (2 CPU/1024MB VBox
NAT-virtio) and honor §29.

## 36. Files Changed

M kernel/drivers/virtio_net.c (mm save/restore +
comment), M kernel/sched/sched.c (wakeup/
can_sleep), M kernel/bench/bench.c (+THREX/
+XSUB, +net.h include), M kernel/bench/bench.h
(decls), M kernel/pm_main.c (wiring),
M kernel/net/net.h (comments),
M docs/LOCKING.md, M docs/SECURITY-MODEL.md,
M docs/DIAGNOSTICS.md, M CHANGELOG.md,
A docs/NOVA-V0.5-PHASE-L-REPORT.md (this file),
A docs/NOVA-V0.5-RELEASE-MANIFEST.md.

## 37. Commit

Single Phase L commit (not pushed):
`feat(release): harden v0.5 integration readiness`.

## 38. Final Verdict

**CONDITIONALLY RELEASE READY.** Phase L found
real defects (L1/L2), fixed them minimally,
proved the fixes, corrected the docs, and leaves
a system whose every release claim carries
in-guest evidence. STOP — await authorization.
