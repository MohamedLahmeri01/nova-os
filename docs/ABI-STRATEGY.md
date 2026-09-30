# NOVA OS — ABI Strategy (Phase 0)

Status: design intent. No numbers are frozen; no ABI is stable yet.

## Principles

1. Never expose kernel structs to userspace. All syscalls take
   scalars and validated buffers/handles only.
2. Version every interface from day one: per-namespace version +
   per-call version, with `sys_abi_version()` query.
3. Separate versioning domains: kernel ABI (syscall numbers/semantics),
   userspace ABI (libc/libnova linkage), application API (headers).
   They move independently.
4. 32/64-bit transparency: no `long`-sized fields on the ABI boundary;
   use fixed-width `u32/u64` + explicit padding; never assume
   `sizeof(void*)` in generic code.

## Namespaces (reserved, not yet numbered)

```text
sys_process sys_thread sys_memory sys_file sys_directory sys_io
sys_network sys_time sys_ipc sys_security sys_system
```

Numbering is explicitly DEFERRED to Phase 5. Any numbers appearing in
Phase 1–4 stubs are provisional and must be marked as such.

## Calling convention (x86-64, planned)

- `syscall` instruction; number in RAX; args in RDI, RSI, RDX, R10, R8, R9.
- Return: non-negative success / negative `-errno` in RAX.
- All user pointers validated: range ∈ user space, length checked for
  overflow, pages probed/locked before use, TOCTOU documented per call.
- 32-bit compat (x86): `int 0x80` or `sysenter` TBD in Phase 5 ADR with
  compat-table design; never alias 64-bit numbers blindly.

## Handles, not pointers

IPC objects, files, processes, mappings are referenced by per-process
handles. Close invalidates. No use-after-close, no cross-process handle
forgery. Rights bits travel with handles (read/write/grant).

## Stability promise

There is NO stability promise before 1.0. Until then every ABI break
bumps the namespace version and lands in the CHANGELOG with a migration
note. 1.0 requires the evidence bar in the master spec §61.

## What Phase 1–4 must do

- Keep a single `kernel/syscall/` table file as the ONLY source of
  numbers (even when empty/provisional).
- Every new privileged entry gets: validation rules, error codes,
  version, and a test (unit + integration) before merge.
