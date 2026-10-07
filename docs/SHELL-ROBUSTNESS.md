# NOVA Shell Robustness (v0.5 Phase J — audit + hardening)

Userspace `init/sh.c` (compiled into init.bin; T23 runs the
same unit with stubbed syscalls). No pipelines, job
control, scripting, globbing, expansion, redirection —
none exists, none added.

## 1. Grammar

`name[ SP args]`, prefix match with NUL/space boundary
(exact commands use full compare). Arguments are
positional, space-separated, command-specific; surplus
text is either the final free-text arg (echo/put text/
wintitle text) or usage-refused. Table: all 30 builtins
in `sh_run` (`userspace/init/sh.c`), each classified in
§9–11 notes below.

## 2. Supported syntax

Command names, space-separated tokens, decimal integers
(via saturating `strtoi`), dotted quads (ping), 3-digit
octal modes (chmod), paths (to kernel caps). Everything
else: usage string or `unknown: try help`.

## 3. Rejected syntax

Unknown names, wrong arg counts/shapes, bad numbers,
empty required args, overlong tokens, trailing garbage
(except free-text tails). `exit with args` does NOT exit
(exact match required — important).

## 4. Input limits

Line 127 chars + NUL (`SH_LINE_MAX` 128); printable
32–126 only (others ignored at input); backspace
guarded at zero; CR/LF terminate. Longer logical lines
cannot exist (input loop enforces, not the parser).

## 5. Token limits

No argv array exists (positional parsing, no tokenizer):
`sh_arg` skips the command word + spaces; `sh_nextnum`
caps tokens at 11 chars + NUL; `put` path capped at 63;
chmod mode exactly 3 octal digits. Nothing unbounded.

## 6. ARGV limits

N/A by architecture (no argv construction; each builtin
walks the line with bounded cursors). Equivalent bounds
in §5 per command.

## 7. Numeric parsing

`nova_strtoi` saturates (overflow refuses, INT_MIN-magnitude
refused, documented); `sh_nextnum` pre-caps at 11 chars;
ping octets saturate the accumulator (Phase J fix —
was benign-but-UB on 10+ digit runs). `+` rejected
everywhere. All negatives refused unless documented
(`rt -1` → usage).

## 8. Path handling

Paths pass through to kernel `copy_user_path` caps
(FS_MAX_PATH) + FS lookup rules (jail-aware). Shell
adds no transformation (no `.`/`..` rewriting, no
trailing-slash stripping): `put` splits path/text at
the first space; empty either side → usage.

## 9. Command dispatch

Ordered prefix/exact chain; shadowing audited
(`rmdir` before `rm`, `wins` exact before `win*`,
`rt`/`run`/`rlimit` pairwise distinct). Unknown →
single usage line. `echo` without boundary check is
the one exception (echoes its suffix; harmless,
documented). No dispatch confusion observed (battery).

## 10. Error recovery

Every builtin returns to `sh_main` loop state (all
state is per-call locals; shared statics are
write-before-read outputs). Invalid → usage/failed →
next prompt. Proven: garbage→help→ticks sequences
produce full valid output (content-checked).

## 11. Parser invariants

- Every read stops at NUL; no read past line end.
- Every number passes saturation before arithmetic.
- Every path/arg is length-capped before use/copy.
- Every syscall arg is kernel-validated again.
- Output per line < 4096B (harness-enforced).
- `echo` is the only boundary-less prefix (by design).

## 12. Fuzzing methodology

Host T23: real `sh.c`, stubbed syscalls (PRINT
captured, rest fail-closed). Exact matrix + 20 corpus
lines + 3000 PRNG-soup (bytes 1–255) + 1000
full-range (0–255, embedded NULs). Fixed seeds,
deterministic. Invariants: return + bounded output
per line; content checks for state-reset.

## 13. Corpus

`tests/corpus/shell/` (mkcorpus.py): empty, spaces,
lengths (127/128/300 via harness, not input loop),
unknown, huge/negative numbers, bad mode, quotes
(literal — no quoting exists), neg click, empty
nslookup, deep put path, ping-huge, exit-args,
plus-num, control bytes. 20 files.

## 14. Resource limits

Line 128B stack · tokens 12B/64B stack · numbers int
(saturated) · output <4KB/line · no heap use at all
(shell allocates nothing — nothing to leak).

## 15. Security properties

Unknown/malformed input resolves to usage/unknown
strings only — never to another command, never to a
privileged op (exit requires exact match; dropcap/
setuid/rlimit re-validate values). FS/net syscalls
re-validate everything (Phase H/I guarantees hold:
shell is one more untrusted caller). No quoting/
globbing/expansion engine to confuse.

## 16. Known limitations

- No quoting/escaping (literal treatment documented;
  `shell-13` pins it).
- Guest keystroke injection not automated (VGA-only
  output unobservable from serial) — real-parser
  coverage is host-side on the identical unit.
- `+` numbers, hex numbers: rejected (documented).
- `help text`, `clear text`: unknown (exact match).
- No history/completion (no state to poison).

## 17. Deferred shell features

Pipelines, job control, scripting, globbing,
expansion, redirection, history — all explicitly out
of scope (prompt §40). None required for robustness.
