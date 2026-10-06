# tests/corpus/shell/ — shell input seeds

One command line per file (no trailing newline), replayed
by T23 through the real `sh_run` with neutered syscalls.
Every line must return with bounded output. Boundary
lengths (127/128/300 vs the 128B line buffer), numeric
overflow (`winopen 9999999999...` — the strtoi UB this
corpus caught), negatives, bad modes, deep paths, quotes/
tabs/high bytes, empty/whitespace. High bytes (0x80/0xFF)
are safe by `unsigned char` comparison discipline —
pinned here, not assumed.
