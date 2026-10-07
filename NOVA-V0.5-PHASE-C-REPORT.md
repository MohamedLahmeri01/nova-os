# NOVA v0.5 Phase C Report — Trace Fetch ABI (implemented)

Decision basis: ADR-0077 + `docs/TRACE-ARCHITECTURE.md` (Phase B).
Commit: `3ef7970` (not pushed — phase state, per v0.5 convention).

## What was built

- `SYS_TRACEFETCH` (36, `SYS_MAX` 36): `nova_tracefetch(buf, max)` —
  `SYS_TRACEFETCH, buf=ebx, max=ecx`. SDK mirrors kernel
  (`sdk/include/nova/nova.h`, `types.h`).
- User record (20B, 4-aligned, no padding, `rsv` always 0):
  `vers=1, kind (1..7), seq, tick, id, arg`. FAULT `arg` projects
  to class (0 null, 1 low/hw, 2 user, 3 supervisor) — raw CR2
  never crosses. All other kinds pass `arg` through.
- Ring record widened 12B→16B with per-emit `seq` (`g_seq`,
  monotonic from boot, never reset by `trace_reset`).
- Handler (`do_tracefetch`): max==0 returns 0 touching nothing
  (pre-validation); max>256 EINVAL; empty ring returns 0;
  newest-N window oldest-first; snapshot under IF, copy under IF=1;
  exact `-errno` propagation, count on success.
- Scope: world-readable (projection, not scope, is the control;
  capability gate is a 3-line change if policy requires it).
- Panic dump prints `seq` (gap visibility).

## Validation

- `trace_fetch_selftest` (boot, real frames + scratch addrspace,
  `TRACEFETCH-OK`): max0-no-touch, 2-mark projection + canaries,
  fault-class projection, badptr EFAULT, over-cap EINVAL,
  256-window shape + strict +1 seqs.
- PERF `trace` block: 200× full-ring fetch, `ticks=1`.
- Gates: `TRACEFETCH-OK` in `run-vbox` + panic markers;
  `docs/ABI.md` regen (row 36). Suite **23/23 + CORPUS-OK**,
  `ci/gate.py` PASS, lint clean (1 pre-existing tolerated warning).

## Bugs found en route (all fixed, all covered)

1. `trace_read` didn't copy `seq` (dTour garbage) — 1-line fix.
2. Selftest canary inside the fetch write window — moved out.
3. `SYSCALL_MAX_RANGE` 4096 < 5120 full fetch (EINVAL) — raised
   to 8192 + host vectors 5000→9000 + SECURITY-MODEL note.
4. Bench `eax` IN+OUT clobber (2nd fetch dispatches #256 =
   ENOSYS) — restore per iteration; same 1-line fix applied to
   the pre-existing dispatch-floor bench (it was timing ENOSYS).

## Deferred / notes

- No shell `trace` command (not in Phase C scope).
- `docs/TRACE-ARCHITECTURE.md` unchanged — implementation matches
  design C+E as written.
- PERF numbers informational: `trace ops=200 ticks=1`,
  `call ops=1000000 ticks=1`.

## Next

STOP. Await Phase D approval.
