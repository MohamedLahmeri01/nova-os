/* NOVA OS virtio-net driver interface (Phase 9b modern-only,
 * Phase 14c interrupts).
 * Transitional virtio-net (1AF4:1000) over BAR2 MMIO: feature
 * subset (MAC + MRG_RXBUF, 12B headers), two queues (RX=0, TX=1).
 * Used-rings are polled (blocking RX is future); interrupts, when
 * the BIOS routed a line, are fielded + counted (net selftest
 * asserts they fire after traffic). Buffers are whole frames
 * (phys-contiguous, <4GB, identity).
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
/* IDT vector for this device (0 when the BIOS left it unrouted).
 * Interrupts expected at all (nonzero vector). Handled count. */
uint32_t virtio_net_irqvec(void);
int virtio_irq_expected(void);
uint32_t virtio_irq_count(void);
/* ISR entry: ack + count (the ring drain lives in the timer
 * tick, not here: no ISR nesting hazard). Runs under the
 * kernel window (MMIO) with IF=0 to completion. */
void virtio_net_irq(void);
/* RX subsystem lock (plain cli/sti: holders run IF=0, and the
 * ISR only runs at IF=1, so lock discipline alone excludes).
 * Implemented here (only TU that may cli); host stubs no-op. */
void net_rx_lock(void);
void net_rx_unlock(void);
/* TEST DRILL (guest selftests only): fake one completed RX
 * frame from a posted buffer and run the real pump path
 * (lock + drain + demux), then restore device progress.
 * Returns diverted count. Single-threaded quiet window only
 * (a real device completion mid-drill is recoverable but
 * loses a frame: see ADR). */
int virtio_net_drill(const uint8_t *ethframe, uint32_t len);
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
