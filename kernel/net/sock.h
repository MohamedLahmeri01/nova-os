/* NOVA OS UDP sockets (Phase 14a, loopback-first).
 * Endpoints bound to ports; loopback destinations (127.0.0.1 or
 * self) deliver into the peer slot, wire destinations reuse the
 * net_udp_send path. Single-slot RX per socket (overwrite
 * semantics, documented like loopback). Sockets need CAP_NET
 * (transmit), live in one global port space (first bind wins),
 * and carry no rlimits (payloads already MTU-capped). Pure logic
 * except the wire leg (host-testable: loopback never touches
 * virtio). All functions return 0 or negative -errno.
 */
#ifndef NOVA_SOCK_H
#define NOVA_SOCK_H

#include <stdint.h>

#define SOCK_MAX 8u
#define SOCK_EPHEMERAL_MIN 49152u
#define SOCK_MTU_PAYLOAD 1472u

#define SOCK_OP_OPEN 0u
#define SOCK_OP_CLOSE 1u
#define SOCK_OP_SEND 2u
#define SOCK_OP_RECV 3u

/* Open a bound endpoint (port 0 = ephemeral pick). Returns the
 * sock id or negative -errno (EMFILE when full, EADDRINUSE when
 * taken, EINVAL on bad port). */
int sock_open(uint16_t port);
/* Close (frees the port; buffered datagram dropped). EBADF on
 * unknown id. */
int sock_close(uint32_t id);
/* Send (loopback slot or wire; fire-and-forget like UDP:
 * success once validated, even with nobody home). */
int sock_send(uint32_t id, uint32_t dst, uint16_t dport,
              const uint8_t *payload, uint32_t len);
/* Receive one datagram payload (copy, clear slot). Blocks when
 * scheduled (delivery/close wakes; close or rebind under a
 * waiter fails it with EBADF); spins in early boot; polls on
 * timeout 0. -EAGAIN on deadline, -EBUSY when another thread
 * already parks here. */
int sock_recv(uint32_t id, uint8_t *out, uint32_t max,
              uint32_t timeout, uint32_t *len_out);
/* Deterministic selftest (bind/loop/close/errors through real
 * paths). Prints SOCK-OK. Nonzero on failure (fatal: panic). */
int sock_selftest(void);
/* Blocking proof (scheduler live): parked waiter woken by a
 * loopback send, deadline expiry, close-under-waiter EBADF.
 * Prints SOCKBLK-OK. Nonzero on failure (fatal: panic). */
int sock_block_selftest(void);
/* Wire demux: one Ethernet frame toward a bound socket port.
 * Returns 1 delivered, 0 dropped (not IP/UDP, not ours, no
 * socket, oversize), -EINVAL on NULL/short frame. Copies
 * straight into the slot (no scratch: IRQ-safe by
 * construction for the future IRQ pump). Untrusted bytes in,
 * validated bytes out (existing eth/IP/UDP parsers). */
int sock_demux_frame(const uint8_t *frame, uint32_t len);

#endif /* NOVA_SOCK_H */
