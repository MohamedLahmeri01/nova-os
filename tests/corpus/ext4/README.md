# tests/corpus/ext4/ — EXT4 mutant manifest

`MUTANTS.txt` rows: `probe offset hex expect  # comment`.
Base image: `build/ext4.raw` from `tools/mkext4.py` (32×4KB
blocks, single group, 128B inodes; DON'T edit offsets
without re-reading it).

Probes against `/HELLO.TXT`: `init` (geometry enforcement:
magic, 4KB, single-group, 128B inodes), `stat`/`read`
(rec_len progress guards, block bounds, size caps).
The `dir-reclen0` row is the infinite-loop guard made
concrete: zero progress must refuse, never spin.
The mtime row is benign by design (still stats).
