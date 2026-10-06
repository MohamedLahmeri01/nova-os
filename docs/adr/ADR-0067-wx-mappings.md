# ADR-0067: W^X without NX + mapping isolation record (Phase C)

Status: accepted (v0.3 Phase C). Date: 2026-10-06.

## Context

v0.2.0 ran the whole kernel RW (every PTE had RW; only page
zero was special). The audit (P1-3) asked for NX/W^X and a
kernel-mapping isolation verdict. This phase measured what
the hardware actually allows instead of assuming.

## Decision

- **Kernel .text is read-only.** `vmm_init` clears RW on
  PT0 pages in `[NOVA_KERNEL_LOAD, text_end)` (linker
  `text_end`, page-rounded); CR0.WP (already set) makes
  supervisor writes fault. `vmm_selftest` asserts
  present+RO on every text page (-7). Cloned per-process
  PDs share PT0, so RO propagates to all spaces by
  construction (verified, not assumed: clone copies PDEs
  raw, PT shared).
- **The change caught a real bug on first boot:** AP
  handoff words (`ap_stack`, `ap_lapic`) lived in `.text`
  and faulted (`trap-PF`, write to present page) at SMP
  bring-up. Fix: moved both words to `.data` (32-bit
  absolute addressing works from any section; the copied
  trampoline reads their link VMAs as before). W^X
  enforcement earning its keep: writable code-adjacent
  state is exactly what it must forbid.
- **NX/XD: detected, honestly unusable.** CPUID
  0x80000001:EDX[20] with max-leaf guard, printed as
  `CPU-NX=0/1` at boot (this VM: 1). Classic non-PAE
  paging has no NX bit, so enforcement stays impossible;
  `XD-ENFORCE=none` is logged, never pretended.
  Enforcement slides to long mode (unchanged from
  ADR-0011).
- **No RWX by intent.** Post-change table (§11): text
  R-- (supervisor-only), data/heap/stacks/bss RW-
  (supervisor-only; executable only because the CPU
  cannot refuse), null page R-- (present, read-only),
  guard absent, user pages U/S with per-map RW,
  MMIO RW- supervisor. `.rodata` stays RW (shares
  `.datax` with `.data`; splitting the link for it buys
  little — rodata corruption is not control flow —
  considered and declined).
- **Kernel-mapping isolation (§14): holds for access.**
  Verified: no `PTE_US` on any kernel mapping (grep over
  mm/ — the bit appears only in user-map paths), so CPL3
  cannot read/write/execute kernel pages, faulting
  instead. Cloning shares the tables, not access.
  Speculative side channels (Meltdown-class) remain out
  of scope per the threat model (no KPTI design yet).
- **ASLR (§15): declined with reason.** No audited
  entropy source on this path (RDRAND presence
  unverified; firmware RNG absent), and deterministic
  layout is load-bearing for tests (fixture addresses,
  selftest windows, drills). Layout stays deterministic;
  randomization waits for an entropy ADR, not a flag.

## Evidence

`CPU-NX=1 ...` + `VMM-OK` (new -7 asserts) on serial;
`SMP-OK` (APs boot through the relocated slots); full
`nova test` 21/21 (no ABI change).

## Hard lessons

- Making text RO *found* the trampoline words: proof
  that enforcement beats audit for this bug class (the
  words sat in .text since Phase 4c, reviewed many
  times, never flagged).
- Mingw symbol rule applies to linker symbols too: C
  `text_end` links the script's `_text_end` (first
  attempt used `_text_end` in C and failed to link —
  same landmine as the object rule, documented again).

## Consequences

- Kernel code cannot be overwritten at runtime (returns
  vs injected code now require a data page + control
  hijack, not a text patch). Data execution remains
  possible (no NX) — stated, not solved.
- `.datax` stays RW (rodata included); a future split
  needs link churn + measured benefit first.
- PAE/long-mode work inherits a W^X-ready layout
  (text/data already split at page granularity).
