# ADR-0050: UDP sockets — loopback-first datagrams (14a)

Status: accepted (Phase 14a). Date: 2026-10-06.

## Context

The network stack could transmit (ping, DHCP, UDP helpers) but
no program could converse: every flow was a kernel-internal
request/reply. Sockets give userspace datagrams, loopback-first
(no loopback UDP delivery existed; wire receive stays with the
direct-poll style, like DHCP).

## Decision

- Fixed pool (8), global port space (first bind wins,
  `EADDRINUSE`), ephemeral pick, single-slot RX (overwrite,
  like loopback). Loopback destinations (127.0.0.1, self)
  deliver straight into the peer slot (never touches virtio:
  host-testable); everything else rides `net_udp_send` (ARP,
  routing, and its errors included). Sends are fire-and-forget
  (validated sends succeed with nobody home: UDP semantics).
- One multiplexed `SYS_SOCK` (OPEN/CLOSE/SEND/RECV, caps
  pattern): SEND/RECV/OPEN need `CAP_NET` (network is network;
  the dropcap sandbox loses sockets too), CLOSE is always safe.
  Payloads cross via validated structs + static bounces (the
  512B stack cap respected; length checked before any touch,
  so oversize never dereferences).
- Shell `uloop <port> <text>` (bind, loop to self, print what
  comes back, close): one command proves send + receive.
- `sock_selftest` prints `SOCK-OK` (bind/dup/ephemeral/fill,
  bad ids, oversize, loopback round-trip with byte check,
  drain silence, close/rebind/drain); gated both lists. Host
  T21 mirrors plus a 2000-op discipline fuzz. No new rlimits
  (payloads MTU-capped); no sender info on delivery (v0.1).

## Evidence

Guest `SOCK-OK`; `nova test` 21/21.

## Hard lessons

- Frozen ticks hang blocking waits: the selftest's empty-recv
  used timeout 5, which spins forever where ticks stand still.
  Immediate recvs (timeout 0) prove the same EAGAIN on both
  platforms. Audit every timeout for host-safety.
- Test order is pool state: the fuzz must follow the draining
  selftest, not precede it (bind expectations assume a clean
  pool). Shared-state tests sequence explicitly.

## Consequences

- Wire *receive* into sockets is future (direct poll stays);
  DNS (resolver + `nslookup`) builds on this next.
- Next: 14b DNS, then IRQ-driven virtio assessment.
