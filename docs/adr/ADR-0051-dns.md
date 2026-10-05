# ADR-0051: DNS resolver — codec-gated, live-opportunistic (14b)

Status: accepted (Phase 14b). Date: 2026-10-06.

## Context

Sockets (14a) gave programs datagrams; names stayed numbers.
A resolver needs a codec, transport, and a server -- and the
only server around (VBox NAT proxy at 10.0.2.3) demonstrably
answers nothing in this environment (three well-formed queries
on the wire, zero replies in pcap). Gating the suite on it
would make red builds a function of upstream connectivity.

## Decision

- RFC 1035 A-record subset: query build (RD, one question,
  strict name grammar) + reply parse (ID/QR/RCODE discipline,
  NXDOMAIN -> ENOENT, every length bounds-checked, compression
  pointers chased with a jump cap, first A wins, CNAME-only
  refuses). Pure functions, crafted-vector proven (full-query
  pin + 8 rogue shapes, guest and host).
- `dns_resolve` reuses the DHCP pattern (ephemeral sport,
  bounded send/recv rounds, ID match) against 10.0.2.3.
  `SYS_DNS` (name in, u32 out) + shell `nslookup`.
- Liveness is informational (`DNS-LIVE ip=` / `DNS-NOLIVE`),
  never a gate: the TX half executes every boot (queries on
  the wire prove it), the RX-match half shares DHCP's proven
  waiter, and the codec -- where rogue input actually bites --
  is fully gated. Manual `nslookup` proves end-to-end wherever
  upstream exists.

## Evidence

Guest codec vectors + `NET-OK`; pcap shows 3 well-formed
queries, 0 replies; `nova test` 21/21.

## Hard lessons

- Prove the direction separately: "queries on the wire" and
  "replies parsed" are independent claims needing independent
  evidence (pcap for the first, crafted vectors for the
  second). A silent server blocks only the conjunction.
- Never gate on someone else's infrastructure: suite-red must
  always mean our bug. Environmental proofs log, they don't
  assert.
## Consequences

- No CNAME following, no AAAA, no caching, no search domains;
  resolver blocks the caller (no async DNS yet).
- Left: IRQ-driven virtio, widgets/toolkit, mouse motion
  (needs talking 8042), secure boot, IOMMU, release work.
