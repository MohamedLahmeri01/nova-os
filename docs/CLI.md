# NOVA OS User Manual — CLI Console (v0.5)

The display is a text console, not a desktop. There is no
mouse pointer, no windows to drag, no menus. You type
commands, the system prints answers. Everything below is
all there is — and all of it works.

## 1. Starting a session

1. Start the `NOVA-OS-CONSTRAINED` VM in VirtualBox.
2. Wait for the boot selftests (about 15 s). You will see
   the demo scene briefly, then the screen clears to the
   console banner:
   `NOVA OS console: keyboard only, no mouse in v0.5`
3. **Click inside the VM window.** Until you do, your keys
   go to Windows, not to NOVA. (This is VirtualBox input
   capture, not a NOVA bug.)
4. Type `help` + Enter. You should see the command list.
5. **Right Ctrl** releases the keyboard back to Windows.

There is intentionally no mouse in v0.5: the guest has no
mouse driver (`MOUSE-ABSENT` in the boot log). Anything a
mouse could do here is done with typed commands instead
(see `click`, `focus`).

## 2. The console (UI/UX)

- 40 columns x 25 rows, white on black, 8x8 font.
- Every line you see is also appended to
  `build/serial.log` on the host: screen and log always
  agree. If the screen ever confuses you, the log is the
  reference copy.
- Prompt is `> ` with an underline cursor. What you type
  echoes as you type it.
- Enter runs the line. Backspace erases one char.
- Lines wrap at column 40 mid-word; long output (e.g.
  `help`) wraps onto several rows — that is normal.
- The screen scrolls (oldest rows fall off the top).
  `clear` scrolls to a blank screen.
- US keyboard layout. Letters, digits, punctuation work.
  Arrow keys, function keys, and Alt-combinations do
  nothing (extended scancodes are dropped by design).
- No command history (Up does nothing), no Tab
  completion, no quoting, no pipes, no scripts: one
  line in, one answer out, up to 128 characters.
- Unknown commands answer `unknown: try help`.
- Failures answer either `denied` (permission/capability
  refusal) or `<what> failed E<num>` (numeric errno, e.g.
  `cat failed E2` = no such file, `ping failed E113` =
  no route). The number is the Linux-compatible errno.

## 3. Guided first session

```text
> help
commands: help echo mem ticks rt ls ...
> ticks
ticks=1234
> mem
frames total=... free=...
> ls
bin
test
disk
...
> cat /test/hello
(prints the file)
> stat
ticks=... switches=...
irqs=... threads=...
pmm_free=...
> ping
(no route plugs print their own message)
> clear
(blank screen, fresh prompt)
```

## 4. Command reference

System / help:

| Command | What it does | Example |
| help | lists every command | `help` |
| echo | prints its arguments | `echo hello` |
| clear | blanks the screen | `clear` |
| ticks | kernel tick counter | `ticks` |
| mem | total + free frames (`frames total=… free=…`) | `mem` |
| stat | ticks, switches, irqs, threads, free frames | `stat` |
| rt [tid] | thread runtime ticks, state, prio (own tid default) | `rt` |
| exit | ends the shell session | `exit` |

Files (paths absolute, `/` is the ramfs root):

| Command | What it does | Example |
| ls [dir] | list names, `/` default | `ls /test` |
| cat path | print a file | `cat /test/hello` |
| mkdir path | make a directory | `mkdir /work` |
| put path text | write text to a file | `put /work/n hi` |
| rm path | delete a file | `rm /work/n` |
| rmdir dir | delete an empty dir | `rmdir /work` |
| fsck | check the native fs | `fsck` |
| run path | execute a program | `run /bin/hi` |

Network (NAT: self 10.0.2.15, gw 10.0.2.2, dns 10.0.2.3):

| Command | What it does | Example |
| ip | self/gateway/netmask | `ip` |
| ping [a.b.c.d] | ping gateway default | `ping 10.0.2.2` |
| dhcp | (re)run DHCP | `dhcp` |
| uloop port text | loopback UDP send+receive | `uloop 7000 hi` |
| nslookup name | DNS A lookup | `nslookup example.com` |

Identity / limits / security:

| Command | What it does | Example |
| id | uid + gid | `id` |
| setuid uid | `usage: setuid <uid>` | `setuid 1000` |
| caps | show capability mask | `caps` |
| dropcap | `usage: dropcap net\|spawn\|all` | `dropcap net` |
| rlimit | get, or set: `rlimit [nofd\|fsize\|frames\|ticks value]` | `rlimit nofd 8` |
| chmod | `usage: chmod <mode> <path>` | `chmod 644 /work/n` |
| chroot | `usage: chroot <dir>` | `chroot /test` |

Windows / widgets (legacy: they paint raw pixels over
the console — run `clear` afterwards):

| Command | What it does | Example |
| wins | list widget windows | `wins` |
| focus | focused window id | `focus` |
| winopen | `usage: winopen x y w h color` | `winopen 10 10 80 40 1` |
| wintitle | `usage: wintitle id text` | `wintitle 2 hi` |
| winclose | `usage: winclose id` | `winclose 2` |
| click | `usage: click x y` (the mouseless click) | `click 160 100` |

## 5. Troubleshooting

- Nothing I type appears: you have not clicked inside
  the VM window (no capture). Click, then type.
- I clicked and the mouse vanished: normal — capture
  hides the host pointer and NOVA draws none. Right
  Ctrl gives it back.
- Screen shows colored blocks, not text: you caught
  mid-boot. Wait ~15 s for the console banner.
- Text I typed is on screen but nothing ran: you
  pressed keys without Enter, or hit the 128-char cap.
- A command misbehaved: re-run it, then compare with
  `build/serial.log` — screen and log are identical by
  construction; report both if they ever disagree.
- Colored rectangles over my text: a widget command
  painted the framebuffer. Run `clear`.
