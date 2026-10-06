# ADR-0066: Syscall copy model — snapshot discipline + TOCTOU closure (Phase B)

Status: accepted (v0.3 Phase B). Date: 2026-10-06.

## Context

The v0.2.0 audit (P0-3) reported cross-thread TOCTOU on
syscall bounce copies: threads share the PD, so a sibling
can rewrite a validated buffer mid-copy. This phase
audited every user-pointer touch (grep-verified complete:
all derefs live in `syscall.c` except MMIO/VRAM; page
state is queried only by `validate_usermem`) and found
the situation better — and more precise — than reported.

## Decision

- **Structural fact (now load-bearing): mappings are
  immutable once created.** No munmap/mprotect/demand-
  paging exists anywhere in the tree. Validated
  present/user/writable bits can therefore never fault-
  escape: fault-TOCTOU is structurally impossible, not
  merely unlikely. The old comment ("single-core makes
  check-then-touch negligible") understated the
  guarantee; the new comment states it exactly.
- **Residual is data tearing only**, bounded by the
  single-pass snapshot rule every site already followed
  and now funnels through two helpers: `copy_from_user`
  / `copy_to_user` (validate-once, copy-once, same errno
  discipline). Kernel decisions run on snapshots only;
  worst case is operating on a different-but-valid input
  — the same semantic as any OS without frozen pages,
  accepted explicitly.
- `copy_user_path` stays a documented exception (NUL-scan
  cannot pre-validate a length; single-pass by
  construction). Single-word stores (`do_meminfo`,
  `do_netinfo`, `do_dns` result) stay direct (validate +
  one store: nothing to tear). `do_winstat` writes kernel
  data out (no user reread: nothing to race).
- Found-and-fixed while centralizing: `do_sock`'s bounce
  buffers were `static` (two concurrent senders/recvrs
  interleaved payloads) and `do_print` read user bytes
  under a whole-print `cli`. Both are per-thread stack
  snapshots now (print's IRQ-hold shrank to kernel-only
  stores as a bonus).
- `validate.h` is declared the SINGLE canonical policy
  (no other file walks page tables for access decisions).
- TOCTOU distinctions (§8): memory — closed above;
  object (fds are integer indices, close invalidates;
  EBADF/EBUSY races gated) — no finding; filesystem
  (paths snapshotted before lookup; refcounts + jail
  floors on snapshots) — no finding; permission (checks
  run on snapshots/current creds, fail-closed order) —
  no finding; lifetime (graveyard, refcounts, waiter
  generations) — no finding. Semantic races beyond data
  tearing: none found; the immutable-mapping invariant
  is what makes that claim hold, and any future paging
  work must re-audit every site in the table below.

## Copy table (§6: every user-pointer syscall)

```text
call        argument      dir    validation           method (now)      max        block  TOCTOU
print       ptr+len       IN     range+pages           copy_from_user    1024       no     torn bytes→log only
getkey      —             —      —                     —                 —          parks  —
meminfo     2×u32 out     OUT    range+pages(w)        single stores     4+4        no     none (kernel values)
ticks       —             —      —                     —                 —          no     —
open        path          IN     NUL-scan+pages        copy_user_path    256+P      no     torn-but-valid path
read        buf+len       OUT    range+pages(w)        copy_to_user/chunk FS_MAX_FILE no    torn output only
write       buf+len       IN     range+pages           copy_from_user/chunk FS_MAX_FILE no torn input snapshot
readdir     path+index    IN     path: NUL-scan        path snapshot     256+P      no     same as open
readdir     namebuf       OUT    range+pages(w)        copy_to_user      16         no     none (kernel data)
mkdir       path          IN     NUL-scan+pages        copy_user_path    256+P      no     same as open
exec        path          IN     NUL-scan+pages        copy_user_path    256+P      no     same as open
fsck        —             —      —                     —                 —          no     —
ping        addr (scalar) —      range check           —                 —          spins  —
dhcp        —             —      —                     —                 —          spins  —
netinfo     3×u32 out     OUT    range+pages(w)        single stores     4×3        no     none (kernel values)
caps/rlim/  scalars       —      value checks         —                 —          no     —
cred/chmod
focus       —             —      —                     —                 —          no     —
winopen     struct        IN     range+pages           field snapshot    sizeof     no     snapshot-then-use
winstat     buf+max       OUT    range+pages(w)+cap    direct fill       93×44      no     none (kernel data)
wintitle    str           IN     range+pages           copy_from_user    16         no     torn-but-valid title
winclose    id scalar     —      —                     —                 —          no     —
unlink/rmdir path         IN     NOVA_EFAULT path      copy_user_path    256+P      no     same as open
sock send   struct+load   IN     struct+payload        snapshot+copy_from MTU       no     snapshot-then-use
sock recv   struct+buf    OUT    struct+buf            snapshot+copy_to  1536       parks  torn output only
dns         name+ip_out   IN/OUT NUL-scan+u32          path snapshot+store 253+P    spins  same as open
winclick    x+y scalars   —      —                     —                 —          no     —
(P = NUL terminator; parks = waiter paths audited in 15-ii/16-i; spins = bounded waits, not idle)
```

## Evidence

Host T8 extended with copy refusal vectors (bad/low/wrap/
over-cap/hole/supervisor/RO/zero-len/NULL-kernel-side, both
directions — all return before any dereference, safe on
host); valid-copy byte paths execute in-guest through every
moved call site (FS content, uloop bytes, title ink all
re-asserted by the unchanged suite). Full `nova test`
21/21 after the refactor (no ABI change: same numbers,
same errnos, same outputs).

## Hard lessons

- The audit's P0-3 read worse than the code: eight sites
  already snapshotted correctly, and immutable mappings
  removed the dangerous half of TOCTOU entirely. Verify
  before severity-labeling — but also: the audit was
  still worth it (found the static-bounce race and the
  print-cli span, both real).
- Centralizing copies is reviewability work more than
  security work here; stated honestly so the next reader
  doesn't re-audit from zero.

## Consequences

- All payload movement has exactly two implementations
  (plus one documented exception). New syscalls must use
  them; reviewers check one place.
- The immutable-mapping invariant is now a security
  assumption with an owner (this ADR): paging work
  re-audits the table above, no exceptions.
- Concurrent-modification stress tests: declined with
  reason (single-pass snapshots make outcomes valid-input
  selections; host threads can't deterministically
  interleave a 3-instruction copy; guest waiter tests
  already prove the scheduling side).
