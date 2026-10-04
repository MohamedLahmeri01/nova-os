/* NOVA OS virtio-net driver interface (Phase 9b, legacy PCI).
 * Transitional virtio-net (1AF4:1000) over I/O BAR0: feature subset
 * (MAC + MRG_RXBUF, 12B headers), two queues (RX=0, TX=1), polled used-rings (no IRQ
 * yet). Buffers are whole frames (phys-contiguous, <4GB, identity).
 * All functions return 0 or negative -errno (see syscall.h).
 */
#ifndef NOVA_VIRTIO_NET_H
#define NOVA_VIRTIO_NET_H

#include <stdint.h>

/* Bring up the first virtio-net device (status handshake, queues,
 * MAC read, RX buffers posted). Prints VIRTIO-OK. Nonzero = absent
 * or broken (caller decides panic vs skip; the recipe guarantees
 * presence on the constrained VM). */
int virtio_net_init(void);
/* Our MAC (valid after init). */
const uint8_t *virtio_net_mac(void);
/* Link ready (init succeeded). Upper layers gate wire paths on this. */
int virtio_net_ready(void);
/* Transmit one Ethernet frame (<=1514B). Synchronous doorbell. */
int virtio_net_tx(const uint8_t *frame, uint32_t len);
/* Poll one received frame into out (<=1514B payload space needed).
 * Returns length or -EAGAIN when none is ready (never blocks). */
int virtio_net_rx(uint8_t *out, uint32_t max);
/* Deterministic selftest (loopback-grade checks that need no peer:
 * MAC unicast bit clear, TX/RX ring accounting, oversize refuse).
 * Wire proof lives in net_selftest (ARP + gateway ping). */
int virtio_net_selftest(void);

#endif /* NOVA_VIRTIO_NET_H */
