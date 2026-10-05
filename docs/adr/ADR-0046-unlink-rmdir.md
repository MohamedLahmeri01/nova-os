# ADR-0046: File lifecycle — unlink, rmdir, node refcounts (13a)

Status: accepted (Phase 13a). Date: 2026-10-05.

## Context

Names were forever: no unlink/rmdir meant test fixtures piled
up in ramfs (`/perm`, `/jail`, ...) and sandboxes could never
clean up after themselves. Deletion needs lifetime rules, or
open descriptions dangle.

## Decision

- `fs_node` gains `unlinked` + `refs`. Opens hold (alloc/free
  pair), jails hold their roots (chroot replace drops the old),
  unlink detaches the name and frees at refs==0: open-but-
  deleted files keep working, POSIX-style. Materialized
  (owns_node) descriptions keep their explicit free path.
- `fs_unlink` (files only, EISDIR on dirs) + `fs_rmdir`
  (empty only: ENOTEMPTY; held roots EBUSY; `/` unreachable
  hence EINVAL) share resolve_parent and the parent-W rule,
  fail-closed like create. `SYS_UNLINK` (30) reaches FAT/NOVA-FS
  through their drivers (EXT4 refuses EROFS); `SYS_RMDIR` (31)
  is ramfs-only (no disk driver removes directories yet).
  Shell `rm`/`rmdir` (+ denied distinct from missing).
- lifecycle proofs ride existing markers: fs_selftest covers
  create/open/unlink/read/drop/recreate + all error legs;
  perm_selftest covers unlink/rmdir denials; jail_selftest
  covers EBUSY with two actors (the jailed root is unnameable
  from inside, so the hold is proven from outside). T9 mirrors
  the lifecycle on host.

## Evidence

Guest `FS-OK`/`PERM-OK`/`JAIL-OK`; `nova test` 20/20.

## Hard lessons

- New shared fields force constructor audits everywhere,
  including `spawn_image` inheritance: the child aliased the
  parent's jail without holding it, so a parent chroot-away
  could zero the count under a live jail (use-after-free).
  `proc_set_jail` unifies dispatch + test + spawn paths.
- Resolve_parent is jail-relative: global spellings fail
  inside jails, which is the confinement working -- but tests
  must address fixtures jail-relatively (`/f`, not the global
  path) or they test ENOENT instead of the property.
- Never submit whitespace-only edits to this toolchain: two
  fused lines from "no-op" replacements cost a debug cycle.
  Every edit must change substantive text.

## Consequences

- Test fixtures still persist only where no test removes them
  (`/test`, `/bin`, `/docs` stay: visible shell proofs).
- Next: 13b candidates (sockets/DNS, CPU quotas) or release.
