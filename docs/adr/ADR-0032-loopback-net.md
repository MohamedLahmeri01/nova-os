# ADR-0032: Loopback-first networking (IPv4 + ICMP + ping)

Status: accepted (Phase 9a). Date: 2026-10-03.

## Context

Phase 9 needs networking without a NIC recipe change (isolation:
NIC none) and without an unverifiable big bang. The roadmap says
loopback first: IPv4 + ICMP echo over a virtual interface prove the
packet path (build, checksum, transmit, serve, parse) before ARP or
drivers exist.

## Decision

`kernel/net/net.c`: big-endian IPv4 build/parse (IHL/length/checksum
validated), ICMP echo build/check (type + ident/seq discipline),
single-slot loopback (tx straight into rx; no queueing yet — v0.1
documents overwrite semantics), synchronous `net_ping` (request ->
loopback serve in place -> reply verify; RTT ticks; non-loopback
is `-EHOSTUNREACH`, never silent). `SYS_PING` + shell `ping
[a.b.c.d]` (hand-rolled octet parser, default 127.0.0.1). Guest
selftest: RFC 1071 vectors, build/parse round-trips, mutation
refusals, empty/oversize discipline, ping + noroute + stats;
`NET-OK` gated. Host T14: same plus 2000-buffer oracle fuzz.

## Evidence

Guest `NET-OK`; typed `ping` -> `reply seq=1 time=0`,
`ping 10.0.0.1` -> `no route to host`; `nova test` 14/14.

## Hard lessons

- Odd-length checksum vectors need an oracle, not mental math
  (7-byte case is 0x2304; verified by execution, pinned in test).
- `nova_strtoi` needs all-digit strings: dotted quads parse octets
  by hand (documented at the call site).
- Test what you assert: checking a request as a reply fails
  correctly — convert-then-check mirrors the serve path.

## Consequences

- No ARP, no NIC, no async/timeouts (loopback is synchronous by
  construction); single loopback slot (callers serialize).
- Next: 9b ARP + virtio-net driver + NIC recipe decision (driver +
  ARP/DHCP/UDP/IP together, per the 8d deferral note).
