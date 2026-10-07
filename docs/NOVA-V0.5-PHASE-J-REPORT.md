# NOVA v0.5 Phase J Report — Shell Robustness (Outcome B, minimal)

## 1. Executive Summary

Audit-first shell hardening: the parser was already
structurally sound (bounded line input, positional
parsing with per-command caps, saturating numerics);
one real defect found (ping octet signed-overflow UB),
fixed with saturation; batteries + corpus + fuzz
extended; no grammar, features, or ABI changes.

## 2. Starting Commit

`c12c15d` (`feat(net): harden network resource and
protocol handling`), tree clean.

## 3. Ending Commit

Single Phase J commit (hash below), tree clean after.

## 4. Baseline Reproduction

23/23, CORPUS-OK 7/7, gate PASS pre-change (all Phase I
markers incl. NETSTRESS-OK).

## 5. Shell Architecture Inventory

Input (VGA kbd → getchar) → 128B line buffer → no
lexer/tokenizer/argv (positional per-builtin cursors:
`sh_arg`/`sh_nextnum`) → 30 builtins → syscalls →
FS/process/net. No heap use anywhere in shell. Map +
per-stage bounds in SHELL-ROBUSTNESS.md §§4–6, 14.

## 6. Current Grammar

`name[ SP args]`; exact or prefix+NUL/space match;
free-text tails (echo/put-text/wintitle-text) or
usage-refusal. Full table §1–3 of doc. No quoting/
escaping/pipes/jobs/scripting (none exists).

## 7. Line Buffer Audit

128B stack, printable-only intake, backspace guarded
at zero, CR/LF terminate, NUL always written. Longer
lines unreachable (input loop, not parser). Empty/
1-char/max/max+1/huge covered (harness feeds longer
C-strings to prove parser-side bounds too).

## 8. Lexer/Tokenizer Audit

None exists (positional parsing). N/A by architecture
— documented, not built.

## 9. Quoting/Escaping Audit

None exists; special bytes treated literally
(`shell-13` pins). No confusion path (nothing to
confuse). Not implemented per scope.

## 10. ARGV Construction Audit

No argv exists. Equivalents bounded per command
(12B num toks, 64B put-path, 3-digit modes, rest of
line <128B). No `argv[argc]==NULL` obligation.

## 11. Numeric Parsing Audit

`strtoi` saturates (INT_MAX/overflow/MIN-magnitude
vectors green); `sh_nextnum` pre-caps 11 chars; ping
octets saturated (Phase J fix); `+`/hex rejected;
negatives refused per-command. v0.4 bug stays fixed.

## 12. Command Dispatch Audit

Ordered chain audited pairwise (rmdir>rm, wins exact
before win*, rt/run/rlimit distinct, all boundaries).
`exit with args` does NOT exit (exact match — proven).
`echo` the single boundary-less prefix (harmless:
echoes suffix; documented). Confusion battery green.

## 13. Builtin Audit

All 30 tabled (min/max argc, parsing, FS/net/syscall
use); arg-count battery covers every arg-taking
builtin (short/long/bad/empty). Malformed → usage/
failed strings; no partial state (all locals).

## 14. Exec Path Audit

`run <path>` → `nova_exec` → kernel copy/validate +
spawn; failures print, shell state untouched (no
locals survive the call). Nondeterminism none.

## 15. Path Handling Audit

Shell transforms nothing; kernel caps + jail-aware
lookup rule. Deep/odd paths via corpus (`put-deep`,
corpus replay). Semantics unchanged.

## 16. Resource Limits

Table §14 of doc: 128B line, 12B/64B toks, saturated
ints, <4KB output/line, zero heap. Nothing unbounded.

## 17. State Reset

Garbage→long→help→ticks sequences produce full valid
output (content-checked `commands:`/`ticks=`), proving
no parser poisoning (nothing to poison: no globals
except write-before-read outputs).

## 18. Error Recovery

Usage/failed/unknown → prompt loop; deterministic;
shell never exits except exact `exit` (proven by
exit-args corpus).

## 19. Control Character Handling

Input loop admits 32–126 only; harness additionally
feeds 0x00–0xFF incl. NUL/DEL/high-bit (1000 lines):
all terminate bounded. Signed-char comparisons safe
(casts or false-branch refusals).

## 20. Integer/Index Safety

`sh_ping` accumulator saturated (the one UB found).
All other arithmetic pre-capped (n≤3 digits then
check; 12-char toks; u32 indices vs 128B lines).
`sh_putdec` 10-digit exact fit for u32.

## 21. User/Kernel Boundary

Shell passes pointers/lengths; kernel re-validates
(copy_user_path caps, per-call checks). No alternate
validation invented. Shell UIDs/caps enforced
kernel-side (setuid/dropcap/rlimit refuse properly).

## 22. Filesystem Interaction

ls/cat/mkdir/put/rm/rmdir/chmod/chroot/run/fsck all
flow through locked, validated syscalls (Phase H
guarantees hold: shell is one more untrusted caller;
single-threaded use).

## 23. Network Interaction

uloop/nslookup/ping/dhcp/stat use validated structs,
capped lengths (text <128B ≪ MTU), timeouts bounded.
Phase I limits respected; no alternate resource path.

## 24. Fuzzing Strategy

T23: real sh.c, stubbed syscalls (PRINT captured,
rest fail-closed). Exact matrix + 20 corpus + 3000
soup (1–255) + 1000 full-range (0–255). Fixed seeds.

## 25. Fuzz Corpus

20 files (16 old + ping-huge/exit-args/plus-num/
ctrl). Lengths/numbers/paths/control coverage.

## 26. Fuzz Invariants

Return + <4KB output per line (harness-enforced);
content checks for reset; capture total bounded
(256KB for the enlarged battery). No panic/crash/
hang/leak possible by construction (no heap, no
loops without NUL/line progress).

## 27. Termination Analysis

Every loop bounded: input (NUL/cap), scans (NUL),
numbers (NUL), fuzz (fixed counts), output (bounded).
Zero-length-progress impossible (each iteration
consumes ≥1 char or returns).

## 28. Real Dispatch Evidence

Host runs the identical translation unit compiled
into init.bin (stub seam only neuters syscalls, not
parsing). Guest keystroke injection not automated
(VGA-only output unobservable via serial — accepted
limitation, documented). Guest USER-OK/HI-OK cover
spawn/run paths.

## 29. Memory/Canary Evidence

No heap in shell (nothing to leak/count). Line/stack
buffers fixed-size with exact-fit proofs (128/12/64/
10-digit). 1000 NUL-bearing lines prove terminator
discipline. No canary instrumentation added
(production paths unchanged; bounds proven by code).

## 30. Performance

Parsing is sub-tick (whole 4020-line battery runs in
the host suite in ms). No kernel-path impact (shell
is userspace; no syscall changes). No optimization.

## 31. Security Review

Unknown/malformed → usage strings only (never another
command, never privileged: exit/dropcap/setuid gates
hold). FS caps/uid/jail, net caps/limits, syscall
validation all re-verified present. No new surface.

## 32. Bugs Discovered

1. PRODUCT BUG (low): `sh_ping` octet accumulator
   signed-overflow UB on 10+ digit runs (benign on
   x86, UB all the same). Fixed by saturation.
2. No other defects. (Three near-misses checked and
   cleared: `sh_putdec` exact-fit OK, `strncmp`
   short-line reads in-bounds, `uloop` length < MTU.)

## 33. Bugs Fixed

#1 (saturation; behavior identical on all inputs).
Regression: `ping-huge` corpus + inline vectors +
existing overflow battery.

## 34. Accepted Limitations

§16 of doc (no quoting, no guest keystroke rig,
`+`/hex rejected, exact-match help/clear/stat,
no history). All deliberate, all documented.

## 35. Deferred Work

Pipelines/jobs/scripting/globbing/expansion/history
— explicitly out of scope, none needed.

## 36. Files Changed

`userspace/init/sh.c` (ping saturation),
`tests/unit/test_shell.c` (confusion/arg/numeric/
reset/full-range batteries), `tools/mkcorpus.py`
(+4 seeds), `tests/corpus/shell/` (+4 files),
`docs/SHELL-ROBUSTNESS.md` (new), `CHANGELOG.md`,
this report.

## 37. Test Matrix

| Test                      | Result | Evidence |
| ------------------------- | ------ | -------- |
| 23/23 baseline            | PASS   | nova test |
| corpus 7/7                | PASS   | CORPUS-OK |
| gates                     | PASS   | ci/gate.py |
| trace fetch               | PASS   | TRACEFETCH-OK |
| trace adversarial         | PASS   | TRACEFETCH-ADV-OK |
| runtime                   | PASS   | RUNTIME-OK |
| FSSTRESS                  | PASS   | FSSTRESS-OK |
| network regression        | PASS   | NETSTRESS-OK + T21/T14 |
| shell unit tests          | PASS   | T23 VERDICT |
| boundary corpus           | PASS   | 20 lines replayed |
| numeric boundary          | PASS   | strtoi + ping vectors |
| argv boundary             | PASS   | n/a (no argv) + equiv caps |
| malformed command battery | PASS   | confusion + arg-count |
| deterministic fuzz        | PASS   | 3000 + 1000 lines |
| real dispatch             | PASS   | identical unit (host) |
| termination battery       | PASS   | all loops bounded |
| memory/canary checks      | PASS   | no-heap + exact fits |
| lint                      | PASS   | 1 pre-existing warning |
| ci/gate.py                | PASS   | gate PASS |
| diff-check                | PASS   | clean |

## 38. Commit

Single Phase J commit (hash below), tree clean after.

## 39. Final Verdict

PASS (Outcome B, minimal). No ADR (no architectural
fork: one saturation fix + tests + docs).
