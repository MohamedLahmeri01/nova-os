# NOVA OS SDK (Phase 11a)

Write user programs against `sdk/include` only. The kernel never
exposes structs: syscalls take scalars + validated buffers (see
`docs/ABI.md`, generated -- never hand-edited).

## Layout

```text
sdk/include/nova/nova.h   syscall numbers, errnos, wrappers
sdk/include/nova/stdio.h  console I/O (puts/putchar/getchar)
sdk/include/nova/string.h freestanding strings + nova_strtoi
sdk/user.ld               link at 0x80000000 (.text.startup entry)
```

## New program checklist

1. Add `userspace/bin/<name>.c` with a `ustart` entry in
   `.text.startup` (see `userspace/bin/uname.c`, the reference:
   version + pid + uid, one `SDK-OK` line, `nova_exit`).
2. Add one manifest row to the `progs` list in `build_userspace`
   (`tools/nova.py`): name + sources (libc objects are shared,
   not recompiled per program).
3. Seed it in `kernel/pm_main.c` `seed_binaries` (`/bin/<name>`)
   and, for boot proof, spawn it after `hi` with its own marker.
4. Gate the marker in `tools/nova.py` (`run-vbox` + panic-test
   lists) and document it in the phase ADR.

Rules: freestanding only (`<stdint.h>` + sdk headers, no hosted
libc), static buffers (no malloc yet), bounded syscalls (print
<= 1024B, paths <= 256B). New syscalls arrive with kernel phases,
never from userspace: extend `docs/ABI.md` via
`python tools/nova.py abi` after any kernel table change
(`nova test` T17 fails when the doc goes stale).
