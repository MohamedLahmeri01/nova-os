# ADR-0073: Fuzzing architecture — corpus, contracts, harnesses (v0.4 J–N)

Status: accepted (v0.4). Date: 2026-10-06.

## Context

v0.3 left fixed-seed PRNG fuzz inline (deterministic, zero
cross-run breadth) and an 11-seed wire corpus. v0.4 §§J–N
demand filesystem corpora, shell/network breadth, a corpus
layout, and a contract per parser — without destabilizing
the suite or inventing harness machinery for its own sake.

## Decision

- **Manifests over binaries for FS mutants.** FS images
  are MB-scale deterministic fixtures; the corpus stores
  `MUTANTS.txt` rows (`probe offset hex expect`), and the
  harness reloads pristine + patches + replays per row.
  Binaries check in only where small and hand-verifiable
  (wire shapes ≤1.5KB, shell lines ≤300B). Rationale:
  reviewable diffs, no binary-blob rot, fixtures stay
  single-sourced from their generators.
- **One manifest, two consumers** (nova_fs): driver rows
  (`init`/`stat`/`read`, exact errno) replay in T12;
  `fsck` rows (nonzero-means-detected) replay in T13's
  tool run. Consumers skip foreign probes. The shared
  file forces driver/tool agreement about what each
  mutant means.
- **Probes are fixed per FS** (`init`/`stat`/`read`, plus
  `ls` where chain-walking needs it): the manifest
  describes inputs, the harness owns semantics. A row
  whose mount breaks is `init`-classified (misplaced
  rows fail as `init-broke`, forcing honest taxonomy).
- **Shell harness via SDK seam** (`NOVA_TEST_STUB_SYSCALL`):
  the alternative (a second shell implementation for
  tests) would test nothing real. The seam is 6 lines in
  the header, guest-invisible (macro undefined → identical
  codegen), and unlocks all future userspace host tests.
  T23 compiles `-m32` so host pointers survive the u32
  ABI exactly (64-bit truncation would fault — same
  class of bug the harness hunts).
- **Queue flood as contract test** (6 frames → 4 kept in
  order + drop-newest + empty timeout): the RXQ bound is
  now gated behavior, not a comment.
- **Frozen-tick discipline**: host tests use timeout-0
  probes only ( nonzero `rx_wait` timeout spins forever
  without ticks — learned the loud way in T21 this
  phase; pinned in docs/FUZZING.md).
- **Bug → seed rule** (docs/FUZZING.md): file + table
  row + fix + ADR note. Precedents this phase:
  strtoi signed-overflow UB (failure would have been
  silent wraparound → usage error; now refused),
  self-loop dir walk (step-cap proof), HELLO-inode
  offset discipline (a wrong-offset manifest row failed
  honestly: design the mutant against the generator
  source, verify bytes before recording).

## Evidence

T10/T11/T12 mutant replays green (7 + 7 + 6 driver rows),
T13 manifest migration green (4 fsck rows), T14 11 seeds +
T21 10 shapes + queue flood green, T23 16 lines + 3000
soup + 8 strtoi vectors green; `CORPUS-OK 7/7` in suite;
full run at commit (Phase U gates on it).

## Hard lessons

- A mutant that cannot fail is not coverage: the first
  `fat-loop` row passed for the wrong reason
  (single-cluster reads stop at size without chasing
  chains). Replaced with a `dir-loop` + index-10 listing
  that actually walks the cap. Design mutants against
  the walk code, not the format diagram.
- Offsets are guilty until verified: the nova manifest's
  first ino-cksum row hit the wrong inode (deterministic
  fixtures make this checkable in one python line —
  always do it before recording).
- `*.bin` inside a C comment trips `-Wcomment`: the
  corpus comment in T21 added the first new warning in
  months. Lint earns its keep continuously, not once.

## Consequences

- Parser work now starts from `docs/FUZZING.md` contracts
  and `tests/corpus/`; inline-only vectors are tech debt
  by policy.
- Remaining gaps (explicit, v0.4+): packet-shape fuzz
  beyond 10 shapes, multi-KB FS images, concurrent-FS
  stress, shell pipelines/quotes (no shell syntax for
  them exists — nothing to fuzz yet).
