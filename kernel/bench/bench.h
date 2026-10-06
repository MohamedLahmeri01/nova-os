/* NOVA OS microbenchmarks (Phase 0.4-P).
 * Reproducible in-guest numbers on serial (PERF lines, ticks at
 * 100Hz): context switches, heap churn, uncontended locks,
 * loopback sockets, ramfs file I/O, user copies, thread
 * create/exit, direct-call floor. Numbers vary with VM jitter:
 * the gate asserts completion + sanity (>0), not exact values
 * (compare runs relatively, never absolutely). Sleep/wake
 * latency lives in SCHED-W; DNS needs live upstream (skipped
 * with reason, like DNS-NOLIVE).
 */
#ifndef NOVA_BENCH_H
#define NOVA_BENCH_H

#include <stdint.h>

/* Run the battery (scheduler + heap + threads live).
 * Prints PERF lines + PERF-OK. Nonzero on failure. */
int bench_selftest(void);

#endif /* NOVA_BENCH_H */
