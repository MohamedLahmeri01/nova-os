# NOVA Trace Architecture (v0.5 Phase B — audit, not implementation)

Status: CURRENT describes code at v0.5 baseline (commit
`1df490c`); Phase C deltas are marked inline (commit
`3ef7970`: 16B records, seq, SYS_TRACEFETCH 36).
REQUIRED states what Phase C must enforce; FUTURE marks
SMP-era work. Nothing here changes behavior.

## 1. Current implementation

`kernel/trace/trace.{h,c}`: one static ring, 256 slots ×
16B (4KB `.bss`, Phase C: +4B seq), kinds 1–7, IF-save emit,
subsequence selftest (`TRACE-OK`), panic tail dump (last 12,
Phase C: prints seq). Seven emit sites total (verified by
grep — no others exist):
thread create ×2 + exit (`thread.c`), sleep park + wake
(`sched.c`, the wake fires from tick-IRQ expiry too),
page fault (`irq.c`, vec 14 only), socket divert
(`sock.c`). `trace_init` in `pm_main` (no cli — safe in
practice: no emitter can fire that early; noted wart,
not a bug). Readers (Phase C): panic dump + `SYS_TRACEFETCH`
(36, projected 20B records, newest-N snapshot; see
`sdk/include/nova/types.h`) + selftests (`TRACE-OK`,
`TRACEFETCH-OK` + Phase D battery `TRACEFETCH-ADV-OK`).
Ring internals touched solely via the API — verified:
no bypass readers in-tree.

## 2. Record layout

```text
offset  size  field  meaning today
0       4     tick   g_ticks at emit (10 ms units, wraps u32)
4       2     kind   1 CREATE(tid,prio) 2 EXIT(tid) 3 SLEEP(tid,deadline)
                      4 WAKE(tid,timedout) 5 FAULT(vec,cr2) 6 RXDIV(port,len)
                      7 MARK(free use)
6       2     id     meaning per kind (see above; tids are small ints)
8       4     arg    meaning per kind (see above)
12      4     seq    per-emit sequence, `g_seq` monotonic from boot,
                    never reset (Phase C; u32 wrap defined mod 2^32)
total 16B, naturally aligned, ZERO padding bytes, all fields
always written (no uninitialized bytes — verified by read).
```

Corrections to earlier summaries: tid IS present (CREATE/
EXIT/SLEEP/WAKE ids); pid is absent everywhere; there is
no cpu_id. Phase C adds the sequence number (see layout);
user records are separate 20B projections, never the raw
ring struct.

## 3. Producer model

| site | context | IF state | can nest? |
|---|---|---|---|
| thread create ×2 | thread, IF=1 | saved+cleared by emit | no (IF=0 inside) |
| thread exit | thread, IF=0 already (cli'd) | saved 0 | no |
| sleep park | thread, IF=0 held | saved 0 | no |
| sleep wake | thread OR tick-IRQ | saved accordingly | no |
| page fault | exception, IF=0 | saved 0 | no |
| socket divert | thread OR tick-IRQ (pump) | saved accordingly | no |

No producer allocates, blocks, panics, or calls back into
the trace. No producer runs on APs (parked). No NMI
sources exist.

## 4. Ring semantics

Single static ring; `g_head` = next-write slot; `g_n` =
held count capped at capacity. Readers: panic dump (last
12, oldest-first) and subsequence search. No per-consumer
cursors, no reservation API, no drain.

## 5. Ordering semantics (exact)

- There IS a global total order today: single executor
  (APs parked) + IF=0 across every emit ⇒ emits are
  strictly serialized in execution order. Ring order ==
  execution order. This is insertion order, and it is
  total — but only under today's executor model.
- NOT chronological by timestamp: ticks repeat across
  records (10 ms granularity vs sub-tick emits).
- NOT stable across reboots (ticks restart; thread IDs
  restart; wire timing differs).
- Deterministic under identical workloads ONLY for
  non-IO paths (thread lifecycle); RXDIV/FAULT arrivals
  depend on wire/device timing.
- Nested emission is impossible (IF=0 inside every
  emit; IRQs cannot fire mid-emit). The header comment's
  reserve-vs-IRQ scenario cannot occur — the guarantee
  is simpler and stronger than documented.

## 6. Interrupt safety (exact mechanism)

`arch_irq_save` (pushf + cli) on entry, `arch_irq_restore`
(sti iff IF was on) on exit. In IRQ/exception context the
save reads 0 and the restore is a no-op: no blind `sti`
mid-handler (the ADR-0061 lesson, applied here first).
Emit touches only `.bss` ring + `g_ticks` (always mapped),
~15 instructions, no branches except the cap check, no
alloc, no slow memory. Fully atomic w.r.t. all other
emitters. Safe in IRQ, exception, and panic-adjacent
contexts (panic runs cli'd; emit needs no locks, unlike
every other subsystem). NOT safe to call before `.bss`
is valid (never happens: first emit runs after boot).
`trace_init` missing cli is safe-in-practice (no emitter
live that early) and documented, not fixed, here.

## 7. SMP status

Classification: **BSP-only by design**. Correctness
rests on single-executor + IF discipline: two CPUs
would race `g_head` reserve/fill (torn slots, lost
counts) and break the total order. SMP needs per-CPU
rings (preferred: no cross-CPU serialization on the
hot path, merge by (tick,seq) at fetch) or one real
lock (simpler, serializes all emits). No change in
Phase B; the requirement is recorded for the SMP ADR.

## 8. Loss semantics

- Empty: count 0, reads refuse ENOENT.
- Partial: reads index oldest-first over held slots.
- Full: overwrite-oldest, count stays 256.
- After wrap: newest 256 survive; evicted records are
  gone from the ring — BUT loss IS detectable since Phase C:
  retained seqs are contiguous (+1) while the gap to any
  previously observed seq measures exactly the records lost
  (proven by the Phase D 600-emit battery: first retained
  seq == base + evicted count).
- Loss is detectable and the lost count IS knowable via
  sequence gaps (Phase C seqnos; see §2). A consumer holding
  the last seen seq subtracts to get the loss. Wrap of the
  u32 seq itself is defined (mod 2^32, gap-ambiguous past
  4B events; effectively never).

## 9. Security classification

SAFE to project later: kind; tick (see note); tid-as-
small-int; port; len/status/timedout/prio/deadline;
bounded counters; sequence numbers (future).
REQUIRES EXPLICIT POLICY: tick (boot-relative timing
reveals uptime/load — low risk, decide in Phase C);
fault CR2 (a kernel address a user probed — project as
fault-class only, or drop); thread existence itself
(process enumeration side channel — decide reader
scope first).
DEFAULT DENY: kernel pointers, CR3/physical addresses,
stack contents/pointers, register dumps, uninitialized
or padding bytes (none exist today — keep it that way),
allocator metadata, lock addresses, FS internals,
device MMIO values, raw ring memory, full structs.
Rule: project fields into a purpose-built record;
never `copy_to_user` a kernel struct (none is ABI-
designed; padding audit would be required per struct).

## 10. Future user ABI requirements (Phase C contract)

Bounded output (caller-sized buffer, truncated-count
semantics TBD); validate output buffer incl. zero-length
(success, count 0), read-only destination (EFAULT),
page-edge spans, `offset+len` overflow; empty ring =
success with count 0; partial reads deterministic
(newest-N or offset-based — Phase C chooses, documents);
loss detection IFF sequence numbers land (required if
the ABI promises it); stable layout + explicit size +
version field + error semantics; ABI freshness test
extends T17 pattern. Design comparison: A (raw ring)
REJECTED (exposes padding/future fields, no projection);
B (struct memcpy) REJECTED (no kernel struct is ABI-
designed); C (explicit projected records) PREFERRED;
D (one-at-a-time) acceptable fallback if snapshots prove
awkward; E (bounded kernel snapshot then copy) REQUIRED
as the mechanism under C or D (never copy under a lock
the reader doesn't own — snapshot under IF, copy after).

## 11. Performance constraints

Static cost only (no sub-tick clock exists to measure
with): ~15 instructions + pushf/cli/sti per emit; no
alloc; cached BSS. Acceptable in IRQ paths by inspection
(timer tick already does more). Fetch cost unmeasured
(no fetch exists). Rule for Phase F: measure fetch with
empty/full ring; never claim zero overhead.

## 12. Panic/self-test behavior

Panic dump runs cli'd (nova_panic cli's first — verified),
so the tail read is race-free; prints last 12 oldest-
first; layout-coupled to `trace_event` (any record-layout
change must update the dump + selftest readers together —
flagged here so Phase C/D doesn't silently break them).
Panic output goes to serial only (physical console).
Selftest asserts marker subsequences (interleave-proof),
NULL/empty refusals, count discipline, and wrap cap
(Phase C adds: fetch projection/shape/seq battery
`TRACEFETCH-OK`; Phase D adds: argument/boundary/wrap/
IF-discipline battery `TRACEFETCH-ADV-OK`).

## 13. Open questions (for Phase C, not blockers)

Reader scope (any process vs owner vs capability — needs
the visibility decision in ADR-0077); tick exposure
policy; FAULT-arg projection (class-only vs drop);
whether MARK stays user-emittable or becomes
kernel-only; per-CPU merge format (SMP era).

## 14. Phase C prerequisites (all met)

Copy helpers suffice (bounded snapshot + copy); validation
policy covers output buffers; no new timing source needed
(ticks are the stamp); no SMP change needed (BSP-only
documented); panic/selftest readers identified for
co-update.
```

CURRENT / REQUIRED / FUTURE separation: §§1–8 + §12
are CURRENT (source-verified); §9 (policy half),
§10–11 are REQUIRED of Phase C; §7-SMP-half and §13
are FUTURE.
