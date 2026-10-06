/* NOVA OS network interface (Phase 9c: DHCP + UDP + fragments).
 * IPv4 + ICMP echo + UDP datagrams; loopback for ping, wire via
 * virtio-net (Ethernet + ARP). Packet buffers are caller-owned
 * statics (no per-packet heap yet; sockets bring allocation).
 * IP config is runtime (DHCP overwrites the VBox NAT defaults).
 * All functions return 0 or negative -errno (see syscall.h).
 */
#ifndef NOVA_NET_H
#define NOVA_NET_H

#include <stdint.h>

#define NET_MTU 1500u
#define NET_PROTO_ICMP 1u
#define NET_PROTO_UDP 17u
#define NET_PORT_DHCP_CLIENT 68u
#define NET_PORT_DHCP_SERVER 67u
#define NET_ADDR_LOOPBACK 0x7F000001u /* 127.0.0.1, host order */
#define NET_ADDR_BCAST 0xFFFFFFFFu
#define ETH_TYPE_IP 0x0800u
/* VBox NAT defaults = initial runtime config (DHCP overwrites). */
#define NET_ADDR_SELF 0x0A00020Fu    /* 10.0.2.15 */
#define NET_ADDR_GW 0x0A000202u      /* 10.0.2.2 */
#define NET_ADDR_MASK 0xFFFFFF00u

/* Runtime IP config (init = the defaults above; dhcp_acquire sets). */
uint32_t net_self_ip(void);
uint32_t net_gw_ip(void);
uint32_t net_netmask(void);

/* Ones-complement checksum (RFC 1071) over len bytes. */
uint16_t net_checksum(const uint8_t *data, uint32_t len);

/* Parse one RX frame: payload offset or -errno; drops malformed,
 * non-ARP/IP, or not-for-us (quietly). Shared with the socket
 * wire demux (single parser, two consumers). */
int eth_parse(const uint8_t *frame, uint32_t len,
              const uint8_t *mymac, uint16_t *type_out);

/* Build an IPv4 header (20B, no options) into out. Returns the
 * header length (20) or negative -errno. */
int net_ip_build(uint8_t *out, uint32_t src, uint32_t dst, uint8_t proto,
                 uint16_t payload_len);
/* Parse + validate (version, IHL, length, checksum, TTL, no
 * fragments: we never reassemble, so MF/offset refuse). Returns
 * payload offset (= IHL*4) or negative -errno. */
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

/* UDP (Phase 9c, wire only): header + pseudo-header checksum build,
 * strict parse (length field + checksum; 0 checksum = absent, per
 * RFC 768, accepted and documented). Send resolves ARP (broadcast
 * skips it); recv filters eth/IP/ports with a tick timeout. */
int net_udp_build(uint8_t *out, uint32_t src, uint32_t dst,
                  uint16_t sport, uint16_t dport,
                  const uint8_t *payload, uint32_t payload_len);
int net_udp_parse(const uint8_t *pkt, uint32_t len, uint32_t src,
                  uint32_t dst, uint16_t *sport_out,
                  uint16_t *dport_out);
int net_udp_send(uint32_t src, uint32_t dst, uint16_t sport,
                 uint16_t dport, const uint8_t *payload,
                 uint32_t len);
int net_udp_recv(uint8_t *out, uint32_t max, uint16_t sport,
                 uint16_t dport, uint32_t dst_ok, uint32_t timeout,
                 uint32_t *len_out);

/* Single-ingress RX pump (thread context): drain ready ring
 * frames; socket-owned go to slots (counts them), everything
 * else waits in the pending queue for rx_wait. Returns frames
 * diverted to sockets. Legacy rx_wait flows drive the pump, so
 * every boot's DHCP already exercises it. Thread-only for now:
 * the ring drain races an IRQ-side drain (shared last_used),
 * so no IRQ hook until a claim flag lands. */
int net_rx_pump(void);

/* DNS client (Phase 14b, RFC 1035 A-record subset): query build
 * (RD set, single question), strict reply parse (ID + QR +
 * RCODE + bounds on every field, compression pointers chased
 * with a jump cap, first A record wins; CNAME chains refuse:
 * direct answers only). Resolver talks to the NAT DNS proxy
 * (10.0.2.3) with bounded retries. All wire input untrusted. */
#define NET_ADDR_DNS 0x0A000203u /* VBox NAT DNS proxy */
#define DNS_MAX_NAME 253u
int dns_build(uint8_t *out, uint32_t max, uint16_t id,
              const char *name);
int dns_parse(const uint8_t *pkt, uint32_t len, uint16_t id,
              uint32_t *ip_out);
int dns_resolve(const char *name, uint32_t *ip_out);

/* DHCP client (Phase 9c, RFC 2131 subset): DISCOVER -> OFFER ->
 * REQUEST -> ACK/NAK over broadcast UDP, bounded retries, then the
 * runtime config applies (self/gw/mask + gateway re-ARP). Rogue or
 * malformed servers are data, never trust: every option length is
 * checked, XID/chaddr/magic must match, offered IP + mask must be
 * sane. Returns 0 or negative -errno (ENODEV without NIC,
 * ETIMEDOUT on silence). Prints DHCP-OK on success. */
int dhcp_acquire(void);
/* Pure message validator (host-testable): checks op/xid/chaddr/
 * magic, walks options with strict bounds, extracts msgtype +
 * yiaddr/server/mask/router/lease. 0 or -errno (callers ignore
 * bad packets and keep waiting). */
int dhcp_parse_msg(const uint8_t *pkt, uint32_t len, uint32_t xid,
                   const uint8_t *chmac, uint8_t *msgtype_out,
                   uint32_t *yiaddr_out, uint32_t *server_out,
                   uint32_t *mask_out, uint32_t *router_out,
                   uint32_t *lease_out);

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
