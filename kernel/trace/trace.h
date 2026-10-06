/* NOVA OS kernel event trace (Phase 0.4-O).
 * Compact bounded ring for crash diagnostics and scheduler/
 * waiter forensics: 256 events x 12B, overwrite-oldest.
 * IRQ-safe by IF-save (arch_irq_save/restore): emit runs in
 * thread and IRQ context alike (sleep_wake emits from the
 * tick). Readers see a possibly-interleaved but never torn
 * stream; selftests assert marker subsequences, not exact
 * sequences. Panic dumps the tail (see panic.c).
 */
#ifndef NOVA_TRACE_H
#define NOVA_TRACE_H

#include <stdint.h>

/* Event kinds (stable numbers; logged, never reordered). */
#define TRACE_CREATE 1u /* thread created (id=tid) */
#define TRACE_EXIT 2u   /* thread exiting (id=tid) */
#define TRACE_SLEEP 3u  /* park (id=tid, arg=deadline-or-0) */
#define TRACE_WAKE 4u   /* wake (id=tid, arg=timedout) */
#define TRACE_FAULT 5u  /* page fault (id=vec, arg=cr2) */
#define TRACE_RXDIV 6u  /* socket divert (id=port) */
#define TRACE_MARK 7u   /* test marker (id+arg free) */

struct trace_event {
    uint32_t tick;
    uint16_t kind;
    uint16_t id;
    uint32_t arg;
};

void trace_init(void);
void trace_reset(void);
/* Emit (any context; bounded, overwrite-oldest, never fails). */
void trace_emit(uint16_t kind, uint16_t id, uint32_t arg);
/* Read back slot i (0 = oldest currently held): 0 ok, -1 empty. */
int trace_read(uint32_t i, struct trace_event *out);
/* Slots currently held (<= capacity, for readers). */
uint32_t trace_count(void);
/* Deterministic selftest (markers + subsequence readback +
 * overwrite wrap). Prints TRACE-OK. */
int trace_selftest(void);

#endif /* NOVA_TRACE_H */
