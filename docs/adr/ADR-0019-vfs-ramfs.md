# ADR-0019: VFS + RAMFS — in-kernel tree, six file syscalls

Status: accepted (Phase 7a). Date: 2026-10-03.

## Context

Init and the shell run but have no storage: no files, no listing,
no way to grow beyond one baked-in binary. Phase 7 starts with the
simplest real filesystem (RAMFS under a tiny VFS) before any disk
driver (FAT32) or persistent design (NOVA-FS).

## Decision

Single ramfs mount at `/` (`kernel/fs/ramfs.c`: nodes, dot-aware
bounded lookup, doubling file buffers capped at 64KB). Open-file
descriptions + per-process fd tables of 16 (`kernel/fs/file.c`,
`process.fds`). Six syscalls 9-14: open (O_RDONLY/WRONLY/RDWR/CREAT),
read, write, close, readdir (path+index, stateless), mkdir. Every
user pointer/length reuses Phase 5 validation; file I/O chunks
through a 512B kernel bounce buffer (user pages need not be
contiguous). Shell gains ls/cat/mkdir; seed: /hello.txt,
/docs/readme.txt. Guest selftest asserts content, 8KB round-trip,
errors (not faults), fd exhaustion/reuse, prints `FS-OK`.

## Evidence

Guest: `FS-OK` in T5/T6 gates; typed `ls`/`cat /hello.txt`/`mkdir
/t`/`ls /t` over VBox scancodes -> real listings and bytes;
`nova test` 9/9 with new T9. Host (T9): seed/nesting/normalization,
gap-zeroing, exact-64KB cap, readdir counts, 20000-path fuzz vs
errno discipline, `VERDICT: filesystem correct`.

## Hard lessons

- Selftest expectation bugs (22-vs-21 seed length) look exactly like
  kernel bugs in the serial log: count string bytes mechanically.
- mingw emits `__chkstk_ms` for >4KB stack frames (no libgcc on the
  kernel link): keep kernel frames small, loop over offsets instead.
- Interactive scancode proofs are host-side-error-prone (Set-1 codes
  for x/o vs i/h, letter order in mkdir): burst-send one command,
  verify the echo, then proceed.

## Consequences

- No persistence (RAMFS dies with the VM), no exec-by-name yet
  (`run` needs spawn-from-FS: Phase 7b), no disk driver (FAT32 next).
- Strict-priority starvation of the idle thread while the shell
  spins persists (Phase 4d aging).
