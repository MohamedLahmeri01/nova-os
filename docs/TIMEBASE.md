# NOVA Timebase (v0.5 Phase E — audit + decision)

Rule: `g_ticks` counts delivered timer interrupts. It is NOT
wall-clock time. Nothing in-tree requires it to be.

## CURRENT

### Source

PIT channel 0, mode 3 (square wave), divisor 11931
(`1193182/100`, C truncation) → theoretical 100.0094 Hz
(+93 ppm). IRQ0 → vec 32 → `g_ticks++` → `sched_tick()`.
`timer_init` programs the PIT, enables IRQs, and panics
(`timer-dead`) unless 20 ticks arrive.

### `g_ticks` semantics (exact)

- u32, init 0, +1 per delivered IRQ0. Measures interrupt
  delivery, NOT physical elapsed time.
- Delayed/coalesced IRQs retard the count (never advance
  it early). All timeouts below are minimum-waits: they
  can overrun in real time, never fire early.
- Wrap: 2^32 ticks = 497.1 days at nominal rate. Every
  comparison in-tree is unsigned-difference or
  signed-difference form — safe for horizons < 2^31
  ticks (248.5 days). Largest in-tree horizon: 8000 ticks.
- `SYS_TICKS`/`SYS_STATS` expose the raw counter (signed
  `int32` across the ABI: user-visible sign flips at
  248 days; display-only, no consumer depends on it).

### Consumers (all tick-native; none needs sub-tick)

| Consumer | Units | Horizon | Compare | Wall-clock? |
|---|---|---|---|---|
| slice `SCHED_SLICE_TICKS=10` | delivered ticks | 10 | counter | no (100 ms nominal) |
| `sleep_until` deadlines | absolute ticks | <=500 | signed diff | no |
| sock/net RX waits | relative ticks | 200 | unsigned diff | no (~2 s) |
| sock-block waiter (+500) | absolute ticks | 500 | signed diff | no |
| DHCP xid | ticks as entropy | — | none | no |
| DHCP elapsed secs `(t-t0)/100` | truncated s (hint) | session | unsigned | hint only |
| watchdogs (kbd/sync/user/sched tests) | relative ticks | <=8000 | unsigned diff | no |
| `rlimit_ticks` quota | delivered ticks | admin-set | `>=` grow-only | no |
| bench PERF deltas | tick diffs | workload | unsigned | informational |
| trace `tick` stamp | ordering aid | — | none authoritative | NO (seq is order) |
| shell `ticks`/`stat` | display | — | none | no |
| comp `demo_wait`, smp delays, pit liveness | relative ticks | <=2000 | unsigned diff | no |

### Conversions

`(g_ticks - t0) / 100u` (DHCP elapsed): truncation toward
zero, hint semantics (RFC 2131 elapsed-secs is a routing
hint; understatement under lost ticks is harmless).
No other tick↔second math exists. No ms/us/ns anywhere.

### Scheduler / DHCP / trace interaction

- Scheduler clock IS delivered IRQs by design: slices,
  expiry, wake latency (`SCHED-W` bounds) are all counted
  in delivered ticks. Rate-independent by construction.
- DHCP elapsed-secs acceptable as-is (hint, truncated,
  monotonic while ticks advance).
- Trace `tick` = IRQ count at emit. Order comes from
  `seq`, never from `tick`. No timestamps added (frozen
  by Phase E scope; record size unchanged).

## MEASURED (Phase E, Ryzen 5 5500 / VBox 7.2.6 / 2 vCPU)

- Divisor 11931 → 100.0094 Hz theoretical.
- Host-timed 500-tick hlt wait: 5031.9 ms → 99.37 Hz
  (±2% poll uncertainty; VBox emulation not cycle-exact).
  Verdict: nominal 100 Hz confirmed, no precision claimed.
- Guest CPUID: AuthenticAMD fam 19h model 50h (Zen 3),
  TSC present, invariant TSC = 1, APIC present.
  No frequency leaves (80000015/16 read zero).
- TSC/PIT calibration (edge-aligned, 200 ticks, hlt-wait):
  run1 7,200,424,476 → 3.6002 GHz; run2 7,179,647,575 →
  3.5898 GHz (±0.5% tick quantization). Matches host base
  clock 3.6 GHz — TSC runs at base rate here.
- `rdtsc` paired-read cost: 13,464 cycles (~3.7 us) —
  TRAPS to the hypervisor (VMEXIT). Mediated, not cheap.
- Method: temporary guest diagnostic (CPUID/rdtsc prints),
  removed before commit; host PowerShell polling of
  serial markers (script removed). No production timing
  behavior was changed at any point.

## TSC DECISION: DEFER (ADR-0078)

PIT stays authoritative. TSC recorded available but not
adopted: frequency undiscoverable in-guest (zero leaves;
PIT calibration is circular), reads trap (~13k cycles),
hypervisor-mediated value, and zero in-tree consumers
need sub-tick precision. Revisit only with a real
sub-tick consumer + boot calibration + cross-CPU sync
story (SMP era).

## FUTURE (not implemented)

```
raw source (PIT today; TSC candidate later)
  → monotonic kernel ticks (g_ticks today; future name TBD)
  → subsystem units (slices, deadlines, timeouts — tick-native)
  → user ABI (raw counter display only; NO high-res user clock)
```

- A high-res user clock is a side-channel surface change:
  needs its own security review, not a timebase tweak.
- Wall clock / RTC / NTP explicitly out of scope.
- Benchmarks stay tick-granular (0 ticks means "<10 ms",
  never "instant"); no TSC-backed PERF without a new ADR.
