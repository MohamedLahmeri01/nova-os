# ADR-0006: Filesystem — VFS first, RAMFS → FAT32 → EXT4 → NOVA-FS

Status: accepted (Phase 0). Date: 2026-09-29.

## Context

Filesystems are the top source of parser-driven kernel bugs. Shipping
many filesystems early multiplies attack surface without evidence.

## Decision

One VFS/vnode interface; milestone order strictly: RAMFS → FAT32 →
EXT4 (read first) → NOVA-FS (journaled, checksummed, crash-consistent
design — features only when requirements are proven). Each ships with
corruption-injection + fuzz tests.

## Consequences

- No NOVA-FS snapshots/COW until the base design is measured.
- Malformed-image tests are merge blockers for every FS.
