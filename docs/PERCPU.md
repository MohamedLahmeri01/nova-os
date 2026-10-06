# NOVA per-CPU design (v0.4 Phase I — design only, no executor yet)

Status: design record. APs stay parked; nothing here executes
on an AP. Purpose: prevent future SMP work from retrofitting
global state incorrectly (§I). When AP scheduling gets its
ADR, this file becomes the implementation checklist.

## Future structure (resident, cache-aligned, zeroed at boot)

```text
struct percpu {
    uint32_t cpu_id;        /* LAPIC ID (smp_cpu_id today) */
    struct thread *current; /* replaces g_current */
    uint32_t irq_nesting;   /* handler depth (assert 0/1 balance) */
    uint32_t irq_held;      /* IF-save debugging (which lock/cli) */
    uint64_t switches;      /* per-CPU switch count (sched counter) */
    uint64_t ticks;         /* per-CPU tick count (accounting) */
    uint32_t stats_irq[48]; /* per-vector counts (merge of g_irq_count) */
    uint8_t  tmp[256];      /* CPU-local scratch (no sharing, no lock) */
};
```

## Accessor shape (to be implemented with AP scheduling)

```c
struct percpu *cpu_local(void);   /* gs/fs-based or LAPIC-indexed array */
#define this_cpu (cpu_local())
```

Candidates, in preference order: (1) array indexed by
LAPIC-ID→compact-index table (simple, one indirection,
works before segments are set up); (2) gs-base MSR per-CPU
(faster, needs GDT/syscall-state care). No decision yet —
both recorded so the ADR chooses with measurements.

## Migration checklist (what becomes per-CPU, in order)

1. `g_current` → `this_cpu->current` (scheduler core).
2. Tick accounting (`ticks_used` stays per-process; add
   per-thread runtime + per-CPU tick totals).
3. IRQ state (`g_irq_count`/`g_last_vec` → per-CPU vectors).
4. `g_sched_switches` → per-CPU + global sum accessor.
5. Sleep/runqueue locks (LOCKING.md rows) become real locks.
6. PMM/heap free lists → per-CPU caches (measure first).

## Anti-patterns (forbidden in review)

- `smp_processor_id()` implemented as a global: must read
  hardware (LAPIC) or segment state, never a settable global
  (forgery = wrong CPU's data).
- Per-CPU data touched without preemption discipline:
  accessor users must document IF/preempt state (same rules
  as locks: holder ⟹ IF=0 or preempt-disabled).
- Migrating a thread without migrating its per-CPU
  references (no migration exists yet; the rule precedes it).
- Sharing `tmp` scratch across call depth (single static
  buffer per CPU is NOT reentrant across IRQ — nest by
  explicit stacking or don't share).
