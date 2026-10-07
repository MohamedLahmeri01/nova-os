/* NOVA OS shared ABI types (Phase 12d).
 * Structs crossing the syscall boundary, defined ONCE here and
 * included by both sides (sdk user code + kernel): layout drift
 * breaks compilation instead of corrupting memory. Fixed-width
 * fields only, explicit padding, no pointers.
 */
#ifndef NOVA_TYPES_H
#define NOVA_TYPES_H

#include <stdint.h>

/* Window snapshot (SYS_WINSTAT): 44 bytes, 4-aligned. */
struct nova_winstat {
    uint32_t id;
    uint32_t x, y, w, h;
    uint32_t focused;
    uint8_t color;
    uint8_t _pad[3];
    char title[16];
};

/* Open arguments (SYS_WINOPEN): geometry + fill color. */
struct nova_winopen {
    uint32_t x, y, w, h;
    uint8_t color;
    uint8_t _pad[3];
};

/* Socket send (SYS_SOCK/SEND): destination + payload. */
struct nova_sock_send {
    uint32_t dst;
    uint16_t dport;
    uint8_t _pad[2];
    uint32_t buf;
    uint32_t len;
};

/* Socket recv (SYS_SOCK/RECV): buffer + budget + timeout; returns
 * the payload length (>= 0) or negative -errno. */
struct nova_sock_recv {
    uint32_t buf;
    uint32_t max;
    uint32_t timeout;
};

/* Trace fetch record (SYS_TRACEFETCH): projected, fixed layout.
 * 20 bytes, 4-aligned, little-endian (x86 native). Every byte
 * has defined semantics (see below); rsv is always zero.
 *   vers: ABI version (1). kind: 1..7 (same numbers as kernel).
 *   seq: per-emit sequence from boot (u32 wrap documented).
 *   tick: 100 Hz tick at emit (wraps u32, like g_ticks).
 *   id: tid (small int) / fault vec / port / marker id.
 *   arg: prio / timedout / deadline-tick / payload len /
 *        FAULT CLASS (0 null, 1 low/hw, 2 user, 3 supervisor)
 *        — never a raw address. MARK args pass through.
 * NOT present by design: pid, cpu, pointers, CR2/CR3/SP,
 * padding (none exists), kernel addresses of any kind. */
#define NOVA_TRACE_VERS 1u
#define TRACEFETCH_MAX 256u

struct nova_trace_rec {
    uint16_t vers;
    uint16_t kind;
    uint32_t seq;
    uint32_t tick;
    uint16_t id;
    uint16_t rsv;
    uint32_t arg;
};

_Static_assert(sizeof(struct nova_trace_rec) == 20,
               "trace rec size");
_Static_assert(sizeof(((struct nova_trace_rec *)0)->seq) == 4,
               "trace rec seq width");

/* Kernel counters snapshot (SYS_STATS, read-only diagnostics):
 * 20 bytes, 4-aligned. All monotonic except pmm_free. */
struct nova_stats {
    uint32_t ticks;
    uint32_t switches;
    uint32_t irqs;
    uint32_t threads;
    uint32_t pmm_free;
};

#endif /* NOVA_TYPES_H */
