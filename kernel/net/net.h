/* NOVA OS network interface (Phase 9a, loopback first).
 * IPv4 + ICMP echo over a virtual loopback interface (no hardware:
 * transmit queues straight into receive). Packet buffers are caller-
 * owned statics (no per-packet heap yet; sockets bring allocation).
 * All functions return 0 or negative -errno (see syscall.h).
 */
#ifndef NOVA_NET_H
#define NOVA_NET_H

#include <stdint.h>

#define NET_MTU 1500u
#define NET_PROTO_ICMP 1u
#define NET_ADDR_LOOPBACK 0x7F000001u /* 127.0.0.1, host order */
/* VBox NAT defaults (Phase 9b static config; DHCP removes this). */
#define NET_ADDR_SELF 0x0A00020Fu    /* 10.0.2.15 */
#define NET_ADDR_GW 0x0A000202u      /* 10.0.2.2 */
#define NET_ADDR_MASK 0xFFFFFF00u

/* Ones-complement checksum (RFC 1071) over len bytes. */
uint16_t net_checksum(const uint8_t *data, uint32_t len);

/* Build an IPv4 header (20B, no options) into out. Returns the
 * header length (20) or negative -errno. */
int net_ip_build(uint8_t *out, uint32_t src, uint32_t dst, uint8_t proto,
                 uint16_t payload_len);
/* Parse + validate (version, IHL, length, checksum). Returns payload
 * offset (= IHL*4) or negative -errno. */
int net_ip_parse(const uint8_t *pkt, uint32_t len, uint8_t *proto_out,
                 uint32_t *src_out, uint32_t *dst_out);

/* Build an ICMP echo request (type 8) into out (8 + payload). */
int net_icmp_build(uint8_t *out, uint16_t ident, uint16_t seq,
                   const uint8_t *payload, uint32_t payload_len);
/* Validate an echo reply (type 0) and match ident/seq. 0 or -errno. */
int net_icmp_check(const uint8_t *pkt, uint32_t len, uint16_t ident,
                   uint16_t seq);

/* Loopback transmit (queues into receive) + receive (oldest packet).
 * Receive returns length or -EAGAIN when empty. Stats for tests. */
int net_loop_tx(const uint8_t *pkt, uint32_t len);
int net_loop_rx(uint8_t *out, uint32_t max);
void net_loop_stats(uint32_t *tx_out, uint32_t *rx_out);

/* Synchronous ping (request -> reply parsed). Loopback is served
 * inline; wire destinations go via Ethernet+ARP (Phase 9b).
 * Returns RTT in ticks (>= 0) or negative -errno (EHOSTUNREACH off
 * route, ETIMEDOUT on silence). */
int32_t net_ping(uint32_t addr, uint16_t seq);

/* Bring up loopback + deterministic selftest (checksum vectors,
 * build/parse round-trips, ping with payload verify). Prints
 * NET-OK. Nonzero on failure (fatal). */
int net_init(void);
int net_selftest(void);

#endif /* NOVA_NET_H */
