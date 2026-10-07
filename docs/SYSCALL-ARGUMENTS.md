# NOVA Syscall Arguments — per-argument audit (v0.4 Phase B)

Source of truth: `kernel/syscall/syscall.c` at v0.3+; validated
2026-10-06 by grep (every `validate_usermem`, every volatile
user deref, every `copy_user_path` call). Directions: I=input,
O=output. Method codes: S=snapshot struct/scalar first,
H=`copy_from/to_user`, P=`copy_user_path` (NUL-scan),
W=single validated store, F=kernel fill (no user reread),
–=scalar, no memory touched.

```text
#  name      args (dir:type)                        validation              method  max        TOCTOU
0  yield     –                                      –                       –       –          –
1  exit      code:I scalar                          –                       –       –          –
2  print     ptr:I buf, len:I scalar                range+pages             H       1024       torn bytes→log only
3  getpid    –                                      –                       –       –          –
4  gettid    –                                      –                       –       –          –
5  version   –                                      –                       –       –          –
6  getkey    –                                      –                       –       –          parks (kbd waiter)
7  meminfo   total:O u32, free:O u32                range+pages(w) each     W×2     4+4        none (kernel values)
8  ticks     –                                      –                       –       –          –
9  open      path:I str, flags:I scalar             NUL-scan+pages          P       256+P      torn-but-valid path
10 read      fd:I scalar, buf:O buf, len:I scalar   range+pages(w)          H/chunk 64KB       torn output only
11 write     fd:I scalar, buf:I buf, len:I scalar   range+pages             H/chunk 64KB       torn input snapshot
12 close     fd:I scalar                            –                       –       –          –
13 readdir   path:I str, index:I scalar,            NUL-scan+pages          P       256+P      same as open
             namebuf:O buf                          range+pages(w)          H       16         none (kernel data)
14 mkdir     path:I str                             NUL-scan+pages          P       256+P      same as open
15 exec      path:I str                             NUL-scan+pages          P       256+P      same as open
16 fsck     – (no args, no pointers)                –                       –       –          –
17 ping      addr:I scalar (IP, not ptr)            subnet check            –       –          –
18 dhcp      –                                      – (CAP_NET gate)        –       –          spins (bounded)
19 netinfo   3×O u32                                range+pages(w) each     W×3     4×3        none (kernel values)
20 caps      op+mask:I scalars                      value checks            –       –          –
21 rlimit    op+which+value:I scalars               value checks            –       –          –
22 cred      op+id:I scalars                        value checks            –       –          –
23 chmod     path:I str, mode:I scalar              NUL-scan+pages          P       256+P      same as open
24 chroot    path:I str                             NUL-scan+pages          P       256+P      same as open
25 focus     –                                      –                       –       –          cycles focus
26 winopen   args:I struct                          range+pages             S       sizeof     snapshot-then-use
27 winstat   buf:O structs, max:I scalar            range+pages(w)+93cap    F       93×44      none (kernel data)
28 wintitle  id:I scalar, str:I buf                 range+pages             H       16         torn-but-valid title
29 winclose  id:I scalar                            –                       –       –          –
30 unlink    path:I str                             NUL-scan+pages          P       256+P      same as open
31 rmdir     path:I str                             NUL-scan+pages          P       256+P      same as open
32 sock      op:I scalar, a:I scalar/id,            struct snapshot         S       sizeof     snapshot-then-use
             b:I struct-or-scalar                   + payload/buf           H       1536       snapshot+bounded copy
33 dns       name:I str, ip_out:O u32               NUL-scan+grammar; u32   P;W     253+P;4    same as open; none
34 winclick  x+y:I scalars                          – (hit compares)        –       –          –
35 stats     out:O struct                           range+pages(w)          H       20         none (kernel data)
36 tracefetch buf:O recs, max:I scalar             range+pages(w)+256cap  H       256×20     none (projected: fault->class)
(P = NUL terminator)
```

## Invariants (normative — test-backed where noted)

1. **No direct user dereference outside validate+copy.**
   The only volatile user reads/writes in-tree are
   `copy_from/to_user`, `copy_user_path`, single-word
   stores (meminfo/netinfo/dns-out), and struct field
   snapshots (sock/winopen, one pass each). Verified by
   grep; `nova lint` does not yet enforce it (v0.4 backlog:
   lint rule for `volatile` outside validate.c — see §X).
2. **Snapshots before decisions.** Lengths, structs, and
   paths are copied once; all branching runs on the copy.
   A sibling thread can only select a different-but-valid
   input, never a fault or an inconsistent length+buffer
   pair (mappings are immutable: no munmap/mprotect —
   load-bearing, ADR-0066).
3. **Outputs validated before write; never re-read.**
   Kernel fills and single stores are write-only from the
   kernel side. Torn output is the user's problem to
   re-read, not a kernel inconsistency.
4. **No nesting.** No syscall takes a pointer-to-pointer
   (sock/winopen structs hold scalars + one flat
   buffer/len pair, snapshotted). Deep user structures
   do not exist; introducing them requires a walker with
   its own ADR.
5. **Zero length never touches memory** (validator and
   both helpers return 0 before any dereference — T8
   vectors pin this).
6. **Faults become errnos.** Every user touch is preceded
   by validation of the exact range touched (chunk loops
   re-validate per chunk — redundant under immutable
   mappings, kept as defense in depth).

## Deltas vs ADR-0066

ADR-0066 gave the per-call table; this file gives the
per-argument table (§6 requirement) plus the six
normative invariants. No code changed for this table —
it records what the v0.3 refactor established.
