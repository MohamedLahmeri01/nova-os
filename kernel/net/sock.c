/* NOVA OS UDP sockets (Phase 14a, blocking recv 15-ii).
 * Fixed pool, global port space, single-slot RX (overwrite like
 * loopback). Loopback destinations (127.0.0.1, self) deliver
 * straight into the peer slot (never touches virtio: host-
 * testable); everything else rides net_udp_send (ARP, routing,
 * and its errors included). Fire-and-forget sends (UDP
 * semantics: validated sends succeed with nobody home).
 * Receive blocks when scheduled (loopback delivery or close
 * wakes the waiter; rebind under a sleeper fails it with
 * EBADF via the bind generation) and spins in early boot
 * (scheduler dead: selftests). One waiter per socket (EBUSY).
 * Single-core: no locking (no IRQ delivery yet; wakeups run
 * in thread context, expiry in IRQ context on owned fields).
 */
#include <stdint.h>

#include "serial.h"
#include "net/net.h"
#include "net/sock.h"
#include "sched/sched.h"
#include "thread/thread.h"
#include "syscall/syscall.h"
#include "time/time.h"

#define SOCK_LOOPBACK 0x7F000001u

struct sock {
    int used;
    uint16_t port;
    uint8_t rx[SOCK_MTU_PAYLOAD];
    uint32_t rxlen;
    int has;
    /* Blocked receiver (one; woken on delivery/close). */
    struct thread *waiter;
    /* Bind generation (bumps per open; rebind detection). */
    uint32_t gen;
};

static struct sock g_socks[SOCK_MAX];
static uint16_t g_next_ephem = SOCK_EPHEMERAL_MIN;
static uint32_t g_sock_gen; /* 0 = never bound (skipped on wrap) */

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
            g_socks[i].waiter = 0;
            if (++g_sock_gen == 0) {
                ++g_sock_gen;
            }
            g_socks[i].gen = g_sock_gen;
            return (int)i;
        }
    }
    return -NOVA_EMFILE;
}

int sock_close(uint32_t id) {
    if (!valid_id(id)) {
        return -NOVA_EBADF;
    }
    /* A parked receiver fails (EBADF on wake: slot gone). */
    if (g_socks[id].waiter != 0) {
        sched_wakeup(g_socks[id].waiter);
        g_socks[id].waiter = 0;
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
        /* Loopback delivery (no wire, no ARP, no waiting). A
         * parked receiver wakes with the data (field cleared
         * before waking: it can never observe a stale waiter). */
        for (uint32_t i = 0; i < SOCK_MAX; i++) {
            if (g_socks[i].used && g_socks[i].port == dport) {
                for (uint32_t k = 0; k < len; k++) {
                    g_socks[i].rx[k] = payload[k];
                }
                g_socks[i].rxlen = len;
                g_socks[i].has = 1;
                if (g_socks[i].waiter != 0) {
                    struct thread *w = g_socks[i].waiter;
                    g_socks[i].waiter = 0;
                    sched_wakeup(w);
                }
                break;
            }
        }
        return NOVA_ESUCCESS;
    }
    return net_udp_send(net_self_ip(), dst, sport, dport, payload,
                        len);
}

/* Drain one buffered datagram (copy, clear slot). Shared by the
 * fast path, the early-boot spin, and the blocking wakeup. */
static int recv_copy(uint32_t id, uint8_t *out, uint32_t max,
                     uint32_t *len_out) {
    uint32_t n = g_socks[id].rxlen;
    if (n > max) {
        return -NOVA_ENOSPC;
    }
    for (uint32_t k = 0; k < n; k++) {
        out[k] = g_socks[id].rx[k];
    }
    *len_out = n;
    g_socks[id].has = 0;
    return (int)n;
}

int sock_recv(uint32_t id, uint8_t *out, uint32_t max,
              uint32_t timeout, uint32_t *len_out) {
    if (!valid_id(id)) {
        return -NOVA_EBADF;
    }
    if (out == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    /* Fast path: data waiting (no sleep, any context). */
    if (g_socks[id].has) {
        return recv_copy(id, out, max, len_out);
    }
    /* Poll (timeout 0 never sleeps: frozen host ticks stay safe,
     * scheduled or not). */
    if (timeout == 0) {
        return -NOVA_EAGAIN;
    }
    /* Early boot (scheduler dead): legacy spin (selftests). */
    if (!sched_can_sleep()) {
        uint32_t t0 = g_ticks;
        for (;;) {
            if (g_socks[id].has) {
                return recv_copy(id, out, max, len_out);
            }
            if (g_ticks - t0 >= timeout) {
                return -NOVA_EAGAIN;
            }
        }
    }
    /* Blocking path: park till delivery or deadline. One waiter
     * per socket (a second sleeper is EBUSY, never stranded).
     * Woken states: data (copy), close/rebind (EBADF via used +
     * generation), deadline (EAGAIN). Anything else loops back
     * to sleep (defensive: no such waker exists today). */
    if (g_socks[id].waiter != 0) {
        return -NOVA_EBUSY;
    }
    {
        uint32_t deadline = g_ticks + timeout;
        uint32_t gen = g_socks[id].gen;
        struct thread *me = sched_current_thread();
        for (;;) {
            int expired;
            g_socks[id].waiter = me;
            expired = sched_sleep_until(deadline);
            g_socks[id].waiter = 0;
            if (!valid_id(id) || g_socks[id].gen != gen) {
                return -NOVA_EBADF;
            }
            if (g_socks[id].has) {
                return recv_copy(id, out, max, len_out);
            }
            if (expired) {
                return -NOVA_EAGAIN;
            }
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
