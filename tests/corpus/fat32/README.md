# tests/corpus/fat32/ — FAT32 mutant manifest

`MUTANTS.txt` rows: `probe offset hex expect  # comment`.
Base image: `build/fat32.raw` from `tools/mkfat.py` (32KB
superfloppy; DON'T edit offsets without re-reading it).

Probes (all against `/HELLO.TXT`): `init` (mount the patched
image), `stat`, `read` (512B buffer), `ls` (`/DOCS` index 10
— exercises the chain step cap: a self-looped directory
must terminate ENOENT, never hang).

Expectations are exact errnos, predicted from
`kernel/fs/fat32.c` checks and confirmed by run. Notable
semantics pinned here: single-cluster reads stop at size
without chasing chains (`fat-loop` still reads 0 —
correct, not a miss); oversize claims refuse before
walking (`ENOSPC`); the benign timestamp row proves
discrimination (not everything fails).
