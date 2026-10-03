# ADR-0020: Exec-by-name — spawn from FS bytes, `run` builtin

Status: accepted (Phase 7b). Date: 2026-10-03.

## Context

RAMFS stores bytes but the kernel can only run the one baked-in init
blob. Closing the loop (bytes -> process) turns the FS into a program
store and unblocks multi-program userspace before any disk driver.

## Decision

`spawn_image(bytes, size)` generalizes the init spawn (own process/PD/
code/stack, 0-on-failure so callers choose panic vs errno);
`spawn_file(path)` resolves an FS regular file and spawns its bytes
(spawn semantics: new process runs, caller continues; true image
replacement stays future work). Second user program `hi` (own
binary/blob via a multi-program `build_userspace`) seeds `/bin/hi`
at boot; `SYS_EXEC` (15) + shell `run <path>` expose it. Boot spawns
/bin/hi as the exec proof (`HI-OK pid=N` in the log, gated in
run-vbox markers).

## Evidence

Guest: `HI-OK pid=5` at boot; typed `run /bin/hi` -> `child pid=6`
-> `HI-OK pid=6`; `nova test` 9/9 (T9 seeds /bin/hi on host for the
selftest's seed assertion).

## Hard lessons

- The fault image panics synchronously after INIT-OK, before spawned
  threads run: boot-proof markers (HI-OK) belong in run-vbox gates
  only, never in PANIC_MARKERS.
- Shell dispatch-function names collide silently in intent (`sh_run`
  vs a new `run` handler): name exec handlers `sh_exec`.

## Consequences

- No argv/env, no image replacement, no wait/reap (exited children
  zombie-handoff; pids grow). No persistence (still RAMFS).
- Next: FAT32 read (needs disk driver: Phase 8 territory), EXT4-read,
  NOVA-FS design.
