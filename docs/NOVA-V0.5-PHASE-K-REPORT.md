# NOVA v0.5 Phase K Report — Diagnostic Integration

## 1. Executive Summary

Phase K integrates the five audit generations
(A–J) into one observable failure story: trap
tick + PF class decode in the trap line, panic
ticks/tid/pid correlation lines, shell errno
texts (`failed E<num>`), and a 15-item
`docs/DIAGNOSTICS.md` contract. Proven in-guest:
UD fault prints `tick=1195` in TRAP and
`ticks=1195` + `tid=0 pid=6` in PANIC — same
tick, correlated. No ABI/policy/timer changes.

## 2. Starting Commit

`8b1fb92` (Phase J shell hardening). Tree clean,
23/23 + CORPUS-OK 7/7 + all OK markers green.

## 3. Ending Commit

Single Phase K commit including this report
(not pushed; see `git log` — amended once to
record its own hash, content identical).

## 4. Baseline Reproduction

At `8b1fb92`: `lint` clean (1 tolerated
warning), `test` 23/23 + CORPUS-OK, `build` +
`image` + `run-vbox` PASS, serial shows
SCHEDW/SYNC/TRACE/TRACEFETCH/PERF/RUNTIME/
NETSTRESS/FSSTRESS/TRACEFETCH-ADV/GATES-OK.

## 5. Observability Inventory

Pre-change surfaces: incremental trace ring
+ fetch projection + seq/CR2 class; runtime
ticks + SYS_RUNTIME + SYS_STATS; scheduler
observability lines; panic regs/CRx/stack/trace
tail (no tick, no ids, no PF class); trap vec/
err/regs (no tick); shell denied/`failed` texts
(no errno numbers); FS errno+fsck; net errno+
gates; self-test FAIL tags. Full list kept in
the working inventory (§15 map in DIAGNOSTICS).

## 6. Diagnostics Contract

15 items (§1–§15 of `docs/DIAGNOSTICS.md`):
contract-not-framework; trace ordering =
sequence numbers; runtime stats are cumulative
measurements; PF is the only trace-emitting
fault; fault classes fixed (UD ⟂ opcode; GP =
segment/privilege/return-target); self-test -1
is a verdict convention; fuzzy-stats vs
cli-exact-runtime rules; serial-only privilege
boundary; user errno texts stay numeric.

## 7. Trace Subsystem Audit

Emit path: IF-save/restore, overwrite-oldest,
per-emit seq, tick-stamped, IRQ-safe. Fault
enrichment: PF CR2→class projection at emit.
Fetch path: snapshot under IF, validate, copy
after — consumer never sees torn sky. Panic
path: last-12 with seqs. Result: no new lock,
no emit-context change, no ring resize.
## 8. Panic Path Audit

Panic runs cli`d: g_ticks frozen, current frozen,
trace tail unread-torn (single reader, no emit
concurrency post-cli). Pre-K gap: no tick line
(ticks frozen but unprinted), no identity (tid/
pid known to scheduler, unprinted), no PF
context (decode died with the trap frame).
Serial-only privilege: pointers/CRx/regs stay
off the user ABI by construction.

## 9. Exception Entry Audit

Entry: error-code push discipline per vector,
common stub saves regs, dispatches C handler.
Pre-K gap: trap line printed vec/err/regs but
not tick � correlation with trace tail was
manual tick-arithmetic. UD/GP carry no CR2;
only PF faults carry an address, hence PF-only
trace emission (accepted, �15 of contract).

## 10. Trap Tick Hardening

Trap line gains `tick=<g_ticks>` (irq.c). Tick
is the IRQ count, not wall time (TIMEBASE.md):
it orders trap against trace tail seqs, not
against seconds. Zero ABI impact (serial-only).

## 11. PF Class Decode

PF error-code bits decoded to `U/S` (user/
supervisor), `R/W` (read/write), `P/N`
(present/not-present) in the trap line. Decode
is pure (no state, no lock). Non-PF faults
print no class � correct: no address exists
for UD/GP.

## 12. Panic Correlation Lines

Panic gains `ticks=<g_ticks>`, `tid=<id>`,
`pid=<id>` (IDs only, -1 when none). No
pointers, no names, no credentials � identity
without disclosure (DIAGNOSTICS �11). Serial-
only, frozen under cli, zero guest-side cost
when not panicking.

## 13. Shell Errno Hardening

New `sh_err(what, rc)` reporter: `<what> failed
E<num>` for any negative rc. Converted ~20
call sites (netinfo/dhcp/caps/dropcap/id/
chmod/chroot/rm/rmdir/uloop/nslookup/stat/
wins/winopen/winclose/wintitle/meminfo/cat/
mkdir/put/run/ping/fsck). Kept friendly texts:
denials (`denied`), ENOENT (`no such file`),
short-write, `no route to host`, fsck problem
counts, rlimit/setuid/dropcap refusals, rt
bounds messages. Follows sh_ls `-2` literal
convention (SDK has no ENOENT macro).

## 14. Diagnostics Document

`docs/DIAGNOSTICS.md`: 15 items, ~120 lines.
Covers architecture, trace ordering, runtime
stats, scheduler obs, panic/fault reporting,
error-code model (ERROR/FAULT/PANIC + -1
verdict convention), FS diagnostics, net
diagnostics, shell diagnostics, resource
limits, disclosure policy, concurrency rules,
failure-injection policy, reproducibility
procedure, limitations. Each limitation is
accepted, none blocks diagnosis.

## 15. Concurrency Analysis

Trap tick: single volatile read, no lock.
PF decode: pure. Panic lines: under cli,
frozen state. sh_err: userspace, no lock.
No new lock order, no lock held across
sleep, no IF discipline change. Locking doc
(LOCKING.md) unchanged � nothing to add.

## 16. Disclosure Review

New serial fields: tick (counter), tid/pid
(small integers), PF class bits (3 chars).
No pointers, no CRx newly exposed, no
credentials, no packet bytes, no uninit
memory. Shell `E<num>` exposes errno
numbers only � already user-visible as
behavior. DIAGNOSTICS �11 records the rule.
## 17. Fault Taxonomy

UD = undefined opcode (no address, no class).
GP = segment/privilege/return-target violation
(no address, no class). PF = address fault
(CR2 + U/S�R/W�P/N class). Double-fault: no
handler (triple-fault resets � accepted �15).
NMI: no handler (accepted). Taxonomy is
closed: every trap prints vec+name+err+tick.

## 18. Error-Code Model

Linux-compatible numbers, negative on
return. ERROR: recoverable op failure, caller
handles (errno per op, fail-closed order).
FAULT: exception ? trap ? panic, no recovery
in v0.5 (accepted). PANIC: halt with reason
+ evidence (regs/CRx/stack/trace/ticks/ids).
Self-test `-1`: FAIL-tagged assertion, a
verdict convention � never confused with
errno (contract �6).

## 19. Shell Grammar Impact

None. No tokens, no builtins, no dispatch
change. Output-text changes only, all within
the <4KB/line bound. T23 corpus + 3000-line
fuzz + 1000-line full-range fuzz: VERDICT
shell correct.

## 20. Trace Ordering Proof

Sequence numbers are the ordering authority
(never ticks). Panic tail prints seqs; fetch
projection preserves them; loss is visible
as seq gaps. Trap tick orders the trap
against the tail approximately (tick
granularity); seq orders records exactly.

## 21. Runtime Evidence

RUNTIME-OK reproduced: `proc=140 sum=140`,
hog/low split. `query200 ticks=0` (idle
sampling). Panic ticks line reads the same
g_ticks the tick path increments � converged
by construction, confirmed 1195 == 1195.

## 22. In-Guest Correlation Proof

panic-test (UD fault kernel): serial shows
`TRAP vec=6 (UD) err=0x00000000 tick=1195
eip=...` then `PANIC: trap-UD`, `ticks=1195`,
`tid=0 pid=6`, `END-PANIC-HALT`. Trap tick ==
panic ticks. Vec name + err + tick + ids on
consecutive lines: the reference correlated
example (DIAGNOSTICS �14).

## 23. Regression: Host Suite

`python tools/nova.py test`: 23/23 passed,
0 failed, 0 skipped. CORPUS-OK 7/7.
T23 post-change: corpus 20 lines + 3000
fuzz + 1000 full-range, VERDICT correct.

## 24. Regression: Guest Suite

`build` + `image` + `run-vbox` PASS (~16�19s,
2 CPU / 1024 MB). Serial: SCHEDW/SYNC/
TRACE/TRACEFETCH/PERF/RUNTIME/NETSTRESS/
FSSTRESS/TRACEFETCH-ADV/GATES-OK all
present. `panic-test` PASS with K-lines.
Main image rebuilt + rebooted after fault
test (fault VDI detached by re-image).

## 25. Gate

`python ci/gate.py`: env + lint + build +
image ? gate PASS. lint: 1 tolerated
warning (syscall.c, pre-existing) + ADR
continuity 0001..0080.

## 26. Fuzzing

No new fuzz target (no parser changed).
Existing batteries re-run green: shell
fuzz (T23), full-range dispatch fuzz,
net/FS stress lines present in-guest.
Deterministic seeds unchanged.

## 27. Performance

No hot-path change: one volatile read in
trap print (fault path only), three prints
in panic (halt path only), sh_err ==
sh_denied-class print (userspace). PERF
lines unchanged in-guest. No measurement
needed beyond absence of delta.

## 28. Memory Safety

No new buffers, no new pointers, no format
strings with untrusted input (tick/ids via
sh_putdec-equivalent %u paths; PF class via
constant chars). Stack usage delta � 0.
Userspace sh_err: locals only, bounded.

## 29. Security Review

Disclosure: �16 clean. Denial-of-diagnosis:
ring overwrite-oldest preserved (no
unbounded growth). Fault-path writes:
serial only, no disk/FS mutation. No new
user ABI: trap/panic are serial-only;
sh_err texts are userspace strings.

## 30. Bugs Discovered

Pre-existing gaps (not regressions): trap
line lacked tick (correlation manual);
panic lacked ticks + identity; PF class
undecoded; ~20 shell sites printed bare
`failed` with errno discarded. All four
fixed in-phase. No new bugs introduced.

## 31. Bugs Fixed

K1 trap tick line. K2 PF U/S�R/W�P/N
decode. K3 panic ticks/tid/pid lines. K4
sh_err + ~20 conversions (incl. put short-
write detection, cat ENOENT-vs-other
split). K5 forward-decl for pre-use sites.
K6 NOVA_ENOENT ? -2 literal per SDK
convention (SDK has no ENOENT macro).

## 32. Accepted Limitations

Per DIAGNOSTICS �15: no fault recovery;
no double-fault handler; PF-only emission;
fuzzy stats; no wall clock; no packet/op
tracing; OOM injection unavailable; no
record/replay. Each accepted with reason.

## 33. Deferred Work

Phase L (awaiting approval): next hardening
slice. No Phase K spillover � all K items
closed. Fault VDI (`nova-os-fault.*`)
regenerated by panic-test; main VDI
restored post-test.

## 34. Files Changed

`kernel/irq/irq.c` (trap tick + PF decode),
`kernel/panic.c` (ticks/tid/pid lines),
`userspace/init/sh.c` (sh_err + conversions),
`docs/DIAGNOSTICS.md` (new, 15 items),
`docs/NOVA-V0.5-PHASE-K-REPORT.md` (new,
this file), `CHANGELOG.md` (K entry).

## 35. ABI Impact

None. No syscall added/changed, no struct
changed, no number changed. SDK untouched.
Serial-line additions are not ABI (no
consumer except eyes + grep).

## 36. Docs Updated

DIAGNOSTICS.md (new), PHASE-K report (new),
CHANGELOG (K entry). TIMEBASE/RUNTIME/
SCHEDULER-OBS/FS-CONCURRENCY/NET-ROBUST/
SHELL-ROBUSTNESS/LOCKING/ABI untouched �
correctly: nothing they state changed.

## 37. Test Matrix

| layer | target | result |
| host | T1�T23 suite | 23/23 |
| host | corpus replay | 7/7 CORPUS-OK |
| host | shell fuzz | 3000 + 1000 full-range green |
| guest | boot markers | all OK incl. ADV/RUNTIME/FS/NET/GATES |
| guest | panic-test UD | PASS, tick 1195 == ticks 1195 |
| gate | env+lint+build+image | PASS |

## 38. Commit

Single Phase K commit (no push): 4 modified
+ 2 new files. Message records tick-proof
`1195 == 1195` and suite state.

## 39. Final Verdict

Phase K COMPLETE. Failure story is now one
correlated record: trap (vec/err/tick/class)
? panic (reason/ticks/tid/pid/regs/tail) ?
shell (E<num> texts) ? contract
(DIAGNOSTICS.md). All green, gate PASS.
STOP � await Phase L approval.
