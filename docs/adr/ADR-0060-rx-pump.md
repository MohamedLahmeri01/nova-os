# ADR-0060: Single-ingress RX — pump + pending queue (15-iv)

Status: accepted (Phase 15, step iv). Date: 2026-10-06.

## Context

The demux (15-iii) was proven but unwired: ring frames
still went straight to `rx_wait`'s direct poll, so a
socket-bound wire packet would sit in the ring until a
matching legacy waiter happened by (never). Wiring the
demux means every frame passes one ingress point -- but
a naive pump that diverts socket frames and drops the
rest starves legacy `rx_wait` flows (DHCP/DNS/ARP/ping).

## Decision

- `net_rx_pump()` (net.c, thread context): drain ready
  ring frames; socket-owned divert to slots (counts
  them), everything else waits in a 4-deep pending queue
  (floods drop newest, documented). Returns diverted.
- `rx_wait` reworked onto the queue: pump each spin,
  serve FIFO head, consume-on-toobig (-ENOSPC, exactly
  like the old ring behavior). Order, errors, and
  timeouts preserved; every legacy flow (ARP, ping, UDP,
  DHCP, DNS) drives the pump as a side effect -- every
  boot's DHCP already exercises the new path.
- Queue slots are full frames (1600B x4, statics); copies
  are field loops (no heap, no alloc failure modes).
- NO IRQ hook yet: the ring drain races an IRQ-side
  drain over shared `last_used`. Pump is thread-only
  until a claim flag lands (that flag is the next step's
  design, discovered here, not deferred by accident).
- NO new guest gate: guest proof is the existing
  DHCP-OK/NET-WIRE-OK (OFFER+ACK traverse pump+queue)
  and SOCKBLK-OK; a gate without new guest behavior is
  hollow. Host T21 proves the pump (divert count, slot
  fill, legacy filtered recv off the queue).

## Evidence

Guest `DHCP-OK ... lease=86400` through the reworked
path; host pump-divert/slot/queue vectors; `nova test`
21/21 (no ABI change).

## Hard lessons

- Two rig fixes, both honest: T14 (net.c without sock.c)
  needed a demux stub; T21's real `net_udp_recv` gates
  on `virtio_net_ready`, so the rig claims NIC-present
  (TX still refuses at the stub doorbell -- the wire
  vector re-derived to -EIO with its new meaning).
- When a stub is replaced by the real thing (second time
  this week: same lesson as the TX vector in 15-iii),
  re-derive every vector touching it; grandfather nothing.

## Consequences

- All RX now flows ring -> pump -> (slots | queue);
  nothing polls the ring directly.
- Next: IRQ-side pump claim flag + `virtio_net_irq`
  hook (wire-to-waiter wake in IRQ context), then more
  sleep callers (getkey/disk).
