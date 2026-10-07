# NOVA v0.5 Phase D Report — Fetch ABI Verification (adversarial)

Baseline first: `ee7c2be` clean, 23/23, CORPUS-OK, gate PASS
— reproduced before any modification. Commit for this phase:
single verification/fix commit (not pushed).

## Contract verified (source: `do_tracefetch`, `validate_usermem`)

`nova_tracefetch(buf, max)`: EAX=36, EBX=buf, ECX=max.
max==0 -> 0 touching nothing (no validation). max>256 ->
EINVAL before any multiply (overflow unreachable: max*20 <=
5120 past the cap). Whole-range validate (present/user/
writable, no-wrap, USER_BASE..USER_END) before any byte.
Snapshot newest-N window oldest-first under IF
save/restore; copy from snapshot with caller's IF; exact
errno; success returns count. Empty ring + valid buf -> 0;
empty ring + bad buf + count -> EFAULT (validated range
nonempty). No cursor, no consumption, no per-process state.

## Battery (`TRACEFETCH-ADV-OK`, guest, real dispatch + scratch spaces)

counts: 0/1/2/256/257/huge/0x10000000; ptrs: NULL/kern-low/
end-cross/wrap/RO/unmapped; spans: page-end exact fit,
1B cross-page, USER_END exact fit vs +1 over, 1-page wide
refuse + no-partial-write (canary + refetch); zero-len under
cli incl. IF-bit check; double-fetch byte identity;
5-emit same-tick order (+1 seqs); 600-emit loss proof
(first retained id/arg/seq == base+344, tail == base+599);
fault-class matrix 0/1/2/3 + CREATE passthrough; failure
preservation refetch. Host T8: 8192 exact, 8193 EINVAL,
2-page hole span (oracle agrees).

## Results

Loss detectable via seq gaps (proven, not merely present).
No address of any kind crosses (CR2->class; no CR3/SP/regs/
ptrs/padding — 20B fully assigned, asserts hold). RO/
unmapped/partial refuse EFAULT with buffers provably
untouched. Failed fetches damage nothing (ring/count/seq
stable, refetch identical). IF discipline holds under
BSP/interrupt model (snapshot atomic; copy from stable
snapshot). Panic seq dump intact (T6 green). PERF
unchanged (trace 200/0-1 ticks, call 1M/1 — timer
granularity). Selftest audit: TRACEFETCH-OK genuinely
dispatches (frames, borrowed CR3, real mappings), ADV
extends it; no weakening anywhere.

## Bugs: ONE

Blind `sti` in `do_tracefetch` (only raw cli/sti site in
`syscall.c`): callers holding IF=0 (all boot selftests,
benches — with foreign CR3 loaded) got IF=1 back. Fix:
save/restore. Regression: IF-bit check across valid+zero
fetches under cli (`if-leak` vector).

## Docs

TRACE-ARCHITECTURE.md §§1-2/8/12 + header updated (were
pre-C: 12B, no seq, "loss unknowable"). SYSCALL-ARGUMENTS
row 36 notes window/zero/short semantics. SECURITY-MODEL
8192B (Phase C). Visibility: world-readable stands as the
recorded Phase C decision (consistent with stats/winstat
posture; projection is the control; 3-line gate reserved).

## Verification

23/23, CORPUS-OK 7/7, GATES-OK, TRACEFETCH-OK,
TRACEFETCH-ADV-OK, PERF-OK, lint clean (1 pre-existing
tolerated warning), `git diff --check` clean, gate PASS.
