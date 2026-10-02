# ADR-0018: Userspace init + shell — own process, three new syscalls

Status: accepted (Phase 6). Date: 2026-10-02.

## Context

The kernel runs self-tests but no persistent user program. Phase 6
adds a real init: own process, own PD, code + stack mapped at
0x80000000/0x80400000, dropping into a line-editing shell with three
new kernel services (keyboard, memory info, tick counter).

## Decision

init+shell ship as ONE static binary (no exec yet; exec arrives with
the FS): `init.bin` (PE -> objcopy binary, 4140B) embedded via
`init_blob.o`, copied to fresh frames, mapped US/RW. Shell builtins:
help, echo, mem, ticks, clear, exit. New syscalls 6-8 (getkey,
meminfo, ticks) reuse the Phase 5 validation path. PS/2 driver on
IRQ1 (Set-1 -> US ASCII, 256B ring, -EAGAIN when empty).

## Evidence

Guest: `INIT-OK`, `NOVA OS init (Phase 6)`, `nova sh (Phase 6)` prompt;
typed `help`/`mem`/`ticks` over VBox scancodes -> `commands: ...`,
`frames total=261864 free=261783`, `ticks=1586`; `nova test` 8/8.

## Hard lessons

- mingw prefixes C symbols with `_`: objcopy's `_binary_*` blob
  symbols must be declared WITHOUT the underscore in C (else the
  object wants a double-underscore name and the link fails).
- `mem_page_state` walked the kernel PD (`g_pd`), so every userspace
  pointer failed validation under a foreign CR3 (silent -EFAULT, no
  output, no panic): it must walk the CURRENT CR3. Diagnosed with a
  temporary idle-loop probe (syscall print counter), removed after.
- The iret entry loads only SS/CS: fresh user threads need DS/ES/FS/GS
  set to 0x23 in the entry trampoline (probes set their own; init
  does not).

## Consequences

- Strict priority without aging starves the idle thread while the
  shell spins on GETKEY: documented, aging is Phase 4d work.
- Blocking input, job control, and exec-by-name stay future work.
