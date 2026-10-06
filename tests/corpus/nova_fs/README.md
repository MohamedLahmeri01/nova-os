# tests/corpus/nova_fs/ — NOVA-FS mutant manifest

`MUTANTS.txt` rows shared by two consumers (each skips
foreign probes): driver rows (`init`/`stat`/`read`,
replayed by T12's `test_nova`) and `fsck` rows (replayed
by T13's `novafsck.py` run, nonzero exit = detected).

Base image: `build/nova.raw` from `tools/mknova.py`
(128 inodes; HELLO.TXT is inode 4 — verify against the
generator before editing offsets; a past off-by-32 error
here is documented in ADR-0073).

Semantics pinned: `stat` resolves via directory data and
never loads inodes (checksum fires on `read`, not `stat`);
bitmaps are allocator/fsck domain (no driver row exists
for the bitmap mutant by design); kill-copy-0 mounts via
copy 1 (`init` 0 + `stat` 0 — the fallback proved, not
assumed).
