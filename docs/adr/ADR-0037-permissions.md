# ADR-0037: UIDs + file ownership and permissions (POSIX-lite)

Status: accepted (Phase 10c). Date: 2026-10-04.

## Context

After capabilities (10a) and limits (10b), every file was still
world-accessible: no owner, no mode bits, no identity switch.
Phase 10 needs discretionary access control with the same
provability bar (a denied access must execute, not just exist).

## Decision

`fs_node` gains uid/gid/mode (9 perm bits; dir-ness stays
separate). Creators own (dirs 755, files 644; boot seeds land
root-owned via the no-current-thread rule). `fs_can` triplets
with root bypass; `fs_open_check` maps access modes (RDWR needs
both). Enforcement: parent-W in `fs_create`/`fs_mkdir`
(fail-closed: before existence checks), open-time checks in
`do_open` (perm before type), R on dirs in `do_readdir`.
`fs_chmod` is owner-or-root, ramfs-only (grafts refuse EROFS:
on-disk permission editing is per-FS future work).
`SYS_CRED` observes/sets uid (root-to-any, self-to-self, else
EPERM; no gid setter yet); `SYS_CHMOD` validates 0777 +
delegates. Foreign filesystems materialize opener-owned
(vfat-style: the opener owns the view, so graft checks pass
vacuously by construction). No traversal checks yet (component
X bits unenforced; open-time gates only). Shell `id`/`setuid`/
`chmod` (decimal digits read as octal) + "denied" errors distinct
from missing. `perm_selftest` proves denials bind through real
and direct paths (nobody fails in /, 0600 binds, triplets
behave, setuid rises refuse); prints `PERM-OK`, gated in
`run-vbox` + `panic-test`. Host T9 gains triplet/open vectors.

## Evidence

Guest `PERM-OK`; `nova test` 16/16.

## Hard lessons

- New identity fields ride every constructor: `node_new`,
  `blank_node`, `g_root`, `process_create`, all stack fakes, and
  the T9 `sched_current_thread` stub (host runs root-privileged).
  The 10b lesson generalizes: shared layers reading new fields
  force a constructor audit across kernel AND tests.
- Dispatch-level open tests are unreachable in selftest (user
  pointers never validate from the kernel CR3): prove the pure
  helpers (`fs_can`, `fs_open_check`) by execution and keep the
  3-line call sites reviewable. Same compromise as 10b's clamp.
- Fail-closed ordering (perm before existence/type) hides
  topology from the denied; existing root-run tests pin the
  permissive paths unchanged.

## Consequences

- Test fixtures persist in ramfs (`/perm`, `/perm/f`: no unlink
  yet; namespaced, enumerated by nothing later in boot).
- DNS option parsed past, group membership lists, setgid,
  traversal X, and native ext4/nova uid passthrough are future.
- Next: 10d filesystem jailing.
