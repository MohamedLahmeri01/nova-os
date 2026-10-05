# ADR-0038: Filesystem jailing (per-process roots, self-restriction)

Status: accepted (Phase 10d). Date: 2026-10-04.

## Context

Capabilities (10a) gate actions, limits (10b) bound amounts,
ownership (10c) guards files -- but every process still saw the
whole namespace. The last Phase 10 slice confines visibility:
a process rooted into a subtree cannot name anything outside it.

## Decision

`struct process` gains `jail_root` (NULL = global view).
Enforcement lives in the fs layer, so every syscall inherits it:
`fs_lookup` starts at the caller's jail (`..` clamps at that
floor via a shared `lookup_at` core), and `fs_create`/`fs_mkdir`
inherit confinement through the parent lookup. Graft branches in
`do_open`/`do_readdir`/`do_mkdir` are skipped for jailed callers
(the ramfs tail then finds no graft names: invisible as ENOENT,
never refused). `SYS_CHROOT` narrows the caller's own view to a
directory resolving inside the current namespace (nested jails
only shrink; nothing unjails -- monotonic like caps/limits).
Spawn inherits the jail (a confined shell's children stay
confined: the `run` escape is closed at lookup). Shell `chroot`.
`jail_selftest` proves confinement with a swapped current:
global names vanish, escapes clamp, nesting shrinks, spawn
targets disappear, harness-only restore; prints `JAIL-OK`,
gated in `run-vbox` + `panic-test`. Host T9 gains `fs_lookup_at`
floor/scope vectors (the shared walk core, explicit roots).

## Evidence

Guest `JAIL-OK`; `nova test` 16/16.

## Hard lessons

- Jail-in-fs-layer beats jail-in-syscalls: one confined walk
  covers open/mkdir/readdir/chmod/chdir-future/spawn, and the
  security property (lookup under a swapped current) executes
  in-guest without user mappings. The only dispatch-only code
  is `do_chroot` itself (~15 lines) plus the graft skip
  conditions -- small, reviewed, shell-demoed.
- Nodes are never freed (no unlink), so jail roots cannot
  dangle: lifetime falls out of an unrelated limitation.
- Fail-invisible (ENOENT) vs fail-refused (EACCES) is a real
  choice: jails hide topology (invisible), permissions refuse
  operations (revealing nothing either, per 10c ordering).

## Consequences

- Test fixtures persist (`/jail` tree, like `/perm`).
- No CWD (absolute paths only; jail composes instead), no
  traversal-X model yet, graft roots unchmodable from inside
  (they are unreachable, which is stronger).
- Phase 10 complete. Next: Phase 11 SDK.
