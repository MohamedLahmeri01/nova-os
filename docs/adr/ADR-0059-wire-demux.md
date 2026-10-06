# ADR-0059: Wire demux — frames to socket slots (15-iii)

Status: accepted (Phase 15, step iii). Date: 2026-10-06.

## Context

Sockets owned the loopback path; wire packets had no
route to a socket slot (`net_udp_recv` serves its own
callers with its own buffers). The missing piece is a
port-match demux over complete Ethernet frames, callable
from a future ring pump (thread or IRQ context).

## Decision

- `sock_demux_frame(frame, len)` (sock.c): eth parse
  (ours/broadcast, IP only) -> IP parse (UDP, dst self/
  broadcast) -> strict UDP parse -> port match -> slot
  fill via the shared `sock_slot_deliver` (same wake
  path as loopback). Returns 1 delivered, 0 dropped,
  -EINVAL on NULL/short. Copies straight into the slot
  (no scratch: IRQ-safe by construction).
- Never truncates: payload over `SOCK_MTU_PAYLOAD` drops
  (reachable only via jumbos: legal MTU frames max out at
  exactly 1472 payload).
- Parser reuse, not duplication: `eth_parse` exported
  (was file-static; single parser, two consumers) with
  `ETH_TYPE_IP` + `NET_ADDR_BCAST` promoted to net.h.
- Proof is host-side T21 (crafted frames through the real
  builders/parsers): hit, wrong port, broadcast MAC,
  ARP ethertype, foreign MAC/IP, short/NULL, jumbo-drop,
  unbound-after-close. T21 now links the real net.c
  (was stubbed); the off-loopback send vector asserts the
  real refusal (-EINVAL, no NIC) instead of the stub's.
- NO new guest gate: nothing calls the demux in-guest
  yet (no pump). A gate without an executor is hollow;
  the guest gate lands with the pump wiring.

## Evidence

Host `VERDICT: sockets correct` (12 demux vectors);
guest build clean (demux linked, uncalled);
`nova test` 21/21 (no ABI change).

## Hard lessons

- Legal MTUs can't overflow the slot (20+8+1472=1500
  exactly): the oversize test needed a hand-built jumbo
  IP header (the builder MTU-caps, correctly). First
  attempt assumed the builder would emit it -- the
  refusal was the code being right, the test wrong.
- Linking real net.c into T21 changed a vector's
  expectation (stub EIO -> real EINVAL): when a stub is
  replaced by the real thing, its vectors must be
  re-derived, not grandfathered.

## Consequences

- Demux proven, unwired: the ring pump (`virtio_net_rx`
  drain -> demux each frame) + IRQ hook is next, with
  the legacy-flow hazard documented (a pump consuming
  the ring starves `net_udp_recv` waiters: DHCP/DNS
  flows must quiesce or migrate first).
- Wire-to-waiter wake (IRQ-context delivery) still open.
