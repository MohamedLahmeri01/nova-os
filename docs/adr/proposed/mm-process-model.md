# Proposed ADR — Memory/process model: capabilities + quotas before names (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

v0.5 has processes (address space + handles),
caps, rlimits, jails, but no mmap/mprotect/COW,
no reaping, no enumeration, no guards. Options:
(a) grow the current model (mmap + COW + guards +
reaping + enumeration as audited extensions);
(b) capability-pure redesign (handles with rights
everywhere, processes as capability spaces);
(c) POSIX-clone semantics (reject: fork-without-
exec copies problems v0.5 deliberately avoided).

## Trade-offs

(a) preserves every v0.5 proof and matches the
appliance profile (spawn, never fork). (b) is
cleaner long-term but rewrites the handle story
(all of file.c/sock.c/process.c) before any
product needs it. Prefer (a) now, steal (b)'s
rights-bits-on-handles idea for the fd/socket
extensions.

## Prerequisites / evidence to decide

M2 NX/W^X/ASLR must land first (new mappings
without protections repeat W2); quota + reaping
proofs (THREX pattern) for each new object type.
