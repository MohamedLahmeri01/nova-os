# NOVA OS v0.8.0 — Troubleshooting

## Nothing on screen after starting

Wait 60 seconds (first boot ~20 s). Then check
the VM is actually running (`showvminfo` state).
If `poweroff` with no output ever: the storage
layout differs from the guide — detach
everything, re-attach exactly per §4, boot again.

## Typing does nothing

Click inside the VM window (input capture). No
click = keys go to the host. Right Ctrl releases.
There is no mouse pointer — normal, not a bug.

## A command says `unknown: try help`

The word is not a command (check spelling with
`help`), or it needs arguments (`help <cmd>`
shows usage). Note: `help` with a wrong name
also answers this way.

## `cat: no such file` / `failed E2`

Path wrong, file deleted, or you are looking on
the wrong volume (`/` vs `/disk` vs `/nova`).
`ls /disk` shows what is really there (names
display UPPERCASE 8.3).

## `put` prints usage

Empty text is refused — give at least one
character. Lines cap at ~100 characters.

## Output looks stale after overwrite

Overwriting with shorter text leaves the old
tail (no truncation). `rm` the file and `put`
again for exact contents.

## `no route to host`

Off-subnet destination: NOVA has no routing by
design (gateway + DNS proxy only). Not a NAT
misconfiguration.

## `exit` froze the prompt

Expected: the session ended (it told you so).
Reset the VM for a new shell.

## Panic screen (TRAP/PANIC + registers + trace)

The system halted with evidence, not silence.
Photograph the whole screen, note the `TRAP`
and `PANIC` lines, record what you typed last,
check whether a fresh VM reproduces it, and
file it — that output IS the bug report.

## Widget pixels over my text

A `winopen`/`click` painted the framebuffer.
Run `clear`.

## Collecting evidence

Screenshots: Machine menu → Take Screenshot.
Serial log (advanced): attach a COM1 file UART
before booting — it mirrors the screen exactly.
Always note: version (`about`), VM config,
artifact hash, exact steps, fresh-VM reproduce.
