/* NOVA OS UDP sockets (Phase 14a).
 * Fixed pool, global port space, single-slot RX (overwrite like
 * loopback). Loopback destinations (127.0.0.1, self) deliver
 * straight into the peer slot (never touches virtio: host-
 * testable); everything else rides net_udp_send (ARP, routing,
 * and its errors included). Fire-and-forget sends (UDP
 * semantics: validated sends succeed with nobody home).
 * Single-core: no locking (IRQ handlers never socket).
 */
#include <stdint.h>

#include "serial.h"
#include "net/net.h"
#include "net/sock.h"
#include "syscall/syscall.h"
#include "time/time.h"

#define SOCK_LOOPBACK 0x7F000001u

struct sock {
    int used;
    uint16_t port;
    uint8_t rx[SOCK_MTU_PAYLOAD];
    uint32_t rxlen;
    int has;
};

static struct sock g_socks[SOCK_MAX];
static uint16_t g_next_ephem = SOCK_EPHEMERAL_MIN;

static int valid_id(uint32_t id) {
    return id < SOCK_MAX && g_socks[id].used;
}

int sock_open(uint16_t port) {
    uint16_t want = port;
    if (port == 0) {
        /* Ephemeral pick: scan up from the cursor (wraps;
         * bounded: the whole range, then give up). */
        uint32_t tries = 0;
        int taken;
        do {
            uint32_t i;
            want = g_next_ephem;
            if (++g_next_ephem < SOCK_EPHEMERAL_MIN) {
                g_next_ephem = SOCK_EPHEMERAL_MIN;
            }
            taken = 0;
            for (i = 0; i < SOCK_MAX; i++) {
                if (g_socks[i].used && g_socks[i].port == want) {
                    taken = 1;
                    break;
                }
            }
            if (!taken) {
                break;
            }
        } while (++tries < 16384u);
        if (taken) {
            return -NOVA_EMFILE;
        }
    } else {
        for (uint32_t i = 0; i < SOCK_MAX; i++) {
            if (g_socks[i].used && g_socks[i].port == port) {
                return -NOVA_EADDRINUSE;
            }
        }
    }
    for (uint32_t i = 0; i < SOCK_MAX; i++) {
        if (!g_socks[i].used) {
            g_socks[i].used = 1;
            g_socks[i].port = want;
            g_socks[i].rxlen = 0;
            g_socks[i].has = 0;
            return (int)i;
        }
    }
    return -NOVA_EMFILE;
}

int sock_close(uint32_t id) {
    if (!valid_id(id)) {
        return -NOVA_EBADF;
    }
    g_socks[id].used = 0;
    g_socks[id].has = 0;
    return NOVA_ESUCCESS;
}

int sock_send(uint32_t id, uint32_t dst, uint16_t dport,
              const uint8_t *payload, uint32_t len) {
    uint16_t sport;
    if (!valid_id(id)) {
        return -NOVA_EBADF;
    }
    if ((payload == 0 && len != 0) || len > SOCK_MTU_PAYLOAD) {
        return -NOVA_EINVAL;
    }
    sport = g_socks[id].port;
    if (dst == SOCK_LOOPBACK || dst == net_self_ip()) {
        /* Loopback delivery (no wire, no ARP, no waiting). */
        for (uint32_t i = 0; i < SOCK_MAX; i++) {
            if (g_socks[i].used && g_socks[i].port == dport) {
                for (uint32_t k = 0; k < len; k++) {
                    g_socks[i].rx[k] = payload[k];
                }
                g_socks[i].rxlen = len;
                g_socks[i].has = 1;
                break;
            }
        }
        return NOVA_ESUCCESS;
    }
    return net_udp_send(net_self_ip(), dst, sport, dport, payload,
                        len);
}

int sock_recv(uint32_t id, uint8_t *out, uint32_t max,
              uint32_t timeout, uint32_t *len_out) {
    uint32_t t0 = g_ticks;
    if (!valid_id(id)) {
        return -NOVA_EBADF;
    }
    if (out == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    for (;;) {
        if (g_socks[id].has) {
            if (g_socks[id].rxlen > max) {
                return -NOVA_ENOSPC;
            }
            for (uint32_t k = 0; k < g_socks[id].rxlen; k++) {
                out[k] = g_socks[id].rx[k];
            }
            *len_out = g_socks[id].rxlen;
            g_socks[id].has = 0;
            return (int)g_socks[id].rxlen;
        }
        if (g_ticks - t0 >= timeout) {
            return -NOVA_EAGAIN;
        }
    }
}

int sock_selftest(void) {
    int a, b, c;
    uint8_t rx[64];
    uint32_t got = 0;
    static const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
    uint32_t i;
    /* Bind + double-bind + ephemeral pack the pool. */
    a = sock_open(5000);
    if (a < 0) {
        serial_puts("SOCK-FAIL open\n");
        return -1;
    }
    if (sock_open(5000) != -NOVA_EADDRINUSE) {
        serial_puts("SOCK-FAIL dup\n");
        return -1;
    }
    b = sock_open(0);
    if (b < 0 || b == a) {
        serial_puts("SOCK-FAIL ephem\n");
        return -1;
    }
    for (i = 0; i + 2u < SOCK_MAX; i++) {
        if (sock_open((uint16_t)(6000u + i)) < 0) {
            serial_puts("SOCK-FAIL fill\n");
            return -1;
        }
    }
    if (sock_open(7000) != -NOVA_EMFILE) {
        serial_puts("SOCK-FAIL full\n");
        return -1;
    }
    /* Bad ids refuse everywhere. */
    if (sock_close(SOCK_MAX) != -NOVA_EBADF ||
        sock_send(SOCK_MAX, SOCK_LOOPBACK, 5000, hello, 5) !=
            -NOVA_EBADF ||
        sock_recv(SOCK_MAX, rx, sizeof(rx), 0, &got) !=
            -NOVA_EBADF) {
        serial_puts("SOCK-FAIL badid\n");
        return -1;
    }
    /* Oversize refuses before touching the payload (length
     * validated first: the pointer is never dereferenced). */
    if (sock_send((uint32_t)a, SOCK_LOOPBACK, 5000, hello,
                  SOCK_MTU_PAYLOAD + 1u) != -NOVA_EINVAL) {
        serial_puts("SOCK-FAIL huge\n");
        return -1;
    }
    /* Loopback round-trip (the money path, no wire involved). */
    if (sock_send((uint32_t)a, SOCK_LOOPBACK, 5000, hello,
                  sizeof(hello)) != 0) {
        serial_puts("SOCK-FAIL send\n");
        return -1;
    }
    if (sock_recv((uint32_t)a, rx, sizeof(rx), 200, &got) != 5 ||
        got != 5u) {
        serial_puts("SOCK-FAIL recv\n");
        return -1;
    }
    for (i = 0; i < 5u; i++) {
        if (rx[i] != hello[i]) {
            serial_puts("SOCK-FAIL bytes\n");
            return -1;
        }
    }
    /* Drained: immediate silence again (timeout 0 keeps this
     * host-safe: frozen ticks must never spin). */
    if (sock_recv((uint32_t)a, rx, sizeof(rx), 0, &got) !=
        -NOVA_EAGAIN) {
        serial_puts("SOCK-FAIL empty\n");
        return -1;
    }
    /* Close frees the port (rebind proves it). */
    if (sock_close((uint32_t)a) != 0 ||
        sock_close((uint32_t)b) != 0) {
        serial_puts("SOCK-FAIL close\n");
        return -1;
    }
    c = sock_open(5000);
    if (c < 0) {
        serial_puts("SOCK-FAIL rebind\n");
        return -1;
    }
    if (sock_close((uint32_t)c) != 0) {
        serial_puts("SOCK-FAIL close2\n");
        return -1;
    }
    /* Drain the rest (clean pool for whatever runs next). */
    for (i = 0; i < SOCK_MAX; i++) {
        if (g_socks[i].used && sock_close(i) != 0) {
            serial_puts("SOCK-FAIL drain\n");
            return -1;
        }
    }
    serial_puts("SOCK-OK\n");
    return NOVA_ESUCCESS;
}
