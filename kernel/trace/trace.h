/* NOVA OS kernel event trace (Phase 0.4-O, seq+fetch Phase C).
 * Compact bounded ring for crash diagnostics and scheduler/
 * waiter forensics: 256 events x 16B, overwrite-oldest, per-
 * emit sequence (loss detection). IRQ-safe by IF-hold across
 * emit (reserve+fill atomic: no interleaved half-records).
 * Readers tolerate overwrite (count caps; read clamps).
 * User reads go through trace_project (lossy: FAULT addresses
 * become classes) + SYS_TRACEFETCH (bounded snapshot).
 * Panic dumps the tail (see panic.c).
 */
#ifndef NOVA_TRACE_H
#define NOVA_TRACE_H

#include <stdint.h>

#include "nova/types.h"

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
    /* Per-emit sequence (Phase C: loss detection). Assigned under
     * the emit lock from g_seq (monotonic from boot, never reset
     * by trace_reset: duplicates across resets would confuse
     * consumers worse than a gap). u32 wrap documented in the ABI
     * (gap-ambiguous past 4B events; effectively never). */
    uint32_t seq;
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
/* Project one internal event into its user-ABI form (pure:
 * no ring access, no locks — host-testable logic, guest-proven
 * here). FAULT addresses project to classes, never cross raw. */
struct nova_trace_rec;
void trace_project(const struct trace_event *in,
                   struct nova_trace_rec *out);
/* Fetch selftest (dispatch through the real syscall with a
 * scratch user mapping): vectors below. Prints TRACEFETCH-OK. */
int trace_fetch_selftest(void);

#endif /* NOVA_TRACE_H */
