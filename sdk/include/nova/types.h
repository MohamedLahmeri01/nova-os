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

#endif /* NOVA_TYPES_H */
