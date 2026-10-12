# NOVA OS — Reproducible Build Plan (Phase M: M0)

PROPOSED plan (no infrastructure changed in this
phase). Goal: any engineer rebuilds the reference
v0.5 image bit-identically and proves it.

## 1. Reference baseline

- Commit: v0.5 `59ea07b` (+ `143576c`, `0b5da64`
  documented extensions; the byte-reference pins
  `59ea07b` until re-pinned by review).
- Recorded toolchain (per RELEASE-MANIFEST.md):
  mingw-w64 gcc 15.2.0 `-m32`, pe-i386 ld,
  custom linker scripts, `CFLAGS32 -mno-sse
  -mno-mmx`, Python 3.11, `tools/nova.py`.
- Procedure records the ACTUAL versions on the
  proving host (`gcc --version`, `ld --version`,
  `python --version`, VBox version) next to the
  expected ones; mismatch = investigate, not fudge.

## 2. Build commands (exact, clean tree)

```text
git status --short            # must show reference state
python tools/nova.py env      # host capabilities
python tools/nova.py clean    # remove prior artifacts
python tools/nova.py build    # kernel.bin + userspace blobs
python tools/nova.py image    # nova-os.raw + .vdi attach
python tools/nova.py run-vbox # boot gate (serial markers)
python tools/nova.py test     # 23/23 + CORPUS-OK
python ci/gate.py             # env + lint + build + image
```

## 3. Hash verification

After `build` + `image`, record SHA-256 of:
`build/kernel.bin`, `build/stage1.bin`,
`images/nova-os.raw`, and each userspace blob.
Reference hashes live in a CHECKSUMS file (to be
created at first M0 run — absent today, stated).
Byte-identical = all hashes match. Merely-
equivalent (same gates, different bytes) is
reported as EQUIVALENT with the diff source
identified (timestamps, paths, ordering) — never
rounded up to identical.

## 4. Known nondeterminism (today's list — extend by measurement)

- Absolute host paths embedded anywhere (grep
  build logs for `F:\` / `/home/` on failure).
- Python dict/set iteration order in image
  assembly (must be sorted or pinned).
- Timestamps in VDI/raw wrappers (normalize or
  exclude wrapper bytes from the hash set).
- Parallel build races (build is serial today —
  keep it serial until proven deterministic).
Each item needs an owner + a fix-or-exclusion
before M0 exit. Unknown unknowns are found by
two-host builds disagreeing — that disagreement
is the method, not a failure.

## 5. Test-environment recording

Every gate run logs: host OS + versions (§1),
VM definition export (`VBoxManage showvminfo`),
serial log preserved per run (never overwritten
in place — rotate with run id), PASS/FAIL per
marker, and on failure the serial tail + the
exact commands. Failures never mutate the
baseline: investigate on a copy, fix forward,
re-run from clean.

## 6. Preserving v0.5

The reference build (binaries + CHECKSUMS +
serial log of a green run + manifest) is archived
read-only. Later milestones diff against it;
deprecation follows the ABI contract (two-release
warning), never a quiet drop.
