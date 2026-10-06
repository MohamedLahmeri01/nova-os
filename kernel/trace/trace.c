/* NOVA OS kernel event trace implementation (Phase 0.4-O).
 * Single static ring (256 x 12B = 3KB, .bss). The reserve-then-
 * fill order under IF-save makes emit atomic w.r.t. interrupts:
 * an IRQ between reserve and fill would observe a half-written
 * slot only if it also emitted — in which case it reserves its
 * OWN next slot (head advanced before fill), so slots never tear
 * and order stays FIFO modulo overwrite. Readers tolerate
 * overwrite (count caps at capacity; read clamps).
 */
#include <stdint.h>

#include "trace/trace.h"
#include "serial.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "trace: no arch backend for this architecture"
#endif

#define TRACE_N 256u

static struct trace_event g_ring[TRACE_N];
static volatile uint32_t g_head;
static volatile uint32_t g_n;

void trace_init(void) {
    g_head = 0;
    g_n = 0;
}

void trace_reset(void) {
    uint32_t f = arch_irq_save();
    g_head = 0;
    g_n = 0;
    arch_irq_restore(f);
}

void trace_emit(uint16_t kind, uint16_t id, uint32_t arg) {
    extern volatile uint32_t g_ticks;
    uint32_t f = arch_irq_save();
    uint32_t i = g_head;
    g_head = (g_head + 1u) % TRACE_N;
    if (g_n < TRACE_N) {
        g_n++;
    }
    g_ring[i].tick = g_ticks;
    g_ring[i].kind = kind;
    g_ring[i].id = id;
    g_ring[i].arg = arg;
    arch_irq_restore(f);
}

uint32_t trace_count(void) {
    return (g_n <= TRACE_N) ? g_n : TRACE_N;
}

int trace_read(uint32_t i, struct trace_event *out) {
    uint32_t n;
    uint32_t idx;
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    n = trace_count();
    if (i >= n) {
        return -NOVA_ENOENT;
    }
    /* Oldest-first: head advanced past the oldest held slot. */
    idx = (g_head + TRACE_N - n + i) % TRACE_N;
    out->tick = g_ring[idx].tick;
    out->kind = g_ring[idx].kind;
    out->id = g_ring[idx].id;
    out->arg = g_ring[idx].arg;
    return NOVA_ESUCCESS;
}

/* Find marker ids in order (subsequence: other events may
 * interleave — IRQs emit during any guest window). */
static int trace_find_mark(uint16_t want_id, uint32_t want_arg,
                           uint32_t from) {
    uint32_t n = trace_count();
    for (uint32_t i = from; i < n; i++) {
        struct trace_event e;
        if (trace_read(i, &e) != 0) {
            return -1;
        }
        if (e.kind == TRACE_MARK && e.id == want_id &&
            e.arg == want_arg) {
            return (int)i;
        }
    }
    return -1;
}

int trace_selftest(void) {
    int a, b, c, i;
    uint32_t n;
    trace_reset();
    if (trace_count() != 0) {
        serial_puts("TRACE-FAIL reset\n");
        return -1;
    }
    if (trace_read(0, 0) != -NOVA_EINVAL) {
        serial_puts("TRACE-FAIL null\n");
        return -1;
    }
    if (trace_read(0, &(struct trace_event){ 0 }) != -NOVA_ENOENT) {
        serial_puts("TRACE-FAIL empty\n");
        return -1;
    }
    trace_emit(TRACE_MARK, 11, 0xA1u);
    trace_emit(TRACE_MARK, 22, 0xB2u);
    trace_emit(TRACE_MARK, 33, 0xC3u);
    n = trace_count();
    if (n != 3u) {
        serial_puts("TRACE-FAIL count\n");
        return -1;
    }
    a = trace_find_mark(11, 0xA1u, 0);
    b = trace_find_mark(22, 0xB2u, 0);
    c = trace_find_mark(33, 0xC3u, 0);
    if (a < 0 || b <= a || c <= b) {
        serial_puts("TRACE-FAIL order\n");
        return -1;
    }
    /* Overwrite wrap: 300 more marks, count caps at 256, the
     * newest marker still found (oldest evicted, never torn). */
    for (i = 0; i < 300; i++) {
        trace_emit(TRACE_MARK, 77, (uint32_t)i);
    }
    if (trace_count() != TRACE_N) {
        serial_puts("TRACE-FAIL cap\n");
        return -1;
    }
    if (trace_find_mark(77, 299u, 0) < 0) {
        serial_puts("TRACE-FAIL wrap\n");
        return -1;
    }
    serial_puts("TRACE-OK\n");
    return NOVA_ESUCCESS;
}
