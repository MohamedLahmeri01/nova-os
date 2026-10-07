# ADR-0080: Socket-pool lock + single-threaded TX contract (v0.5 Phase I)

Status: accepted (v0.5). Date: 2026-10-07.

## Context

Phase I audit: packet parsers sound (bounds-first,
no reassembly, capped queues), but the socket pool —
slot leasing, slot bytes, waiter, generation — mutated
under no lock. Two threads opening concurrently could
alias one slot (scan+claim non-atomic vs preemption);
close-vs-park and deliver-vs-close resolved only by
luck of IF timing. TX/DHCP/DNS statics are likewise
single-threaded by accident, not contract.

## Decision

- One pool spinlock (`g_sock_lock`, irqsave,
  owner=current): open/close/send/deliver/recv-copy.
  Park keeps its verified cli check-and-park (releases
  conceptually; revalidation converges). Nesting:
  pump takes rx_lock then sock lock (rx→sock, the only
  nesting; threads take sock alone) — no cycle.
- TX/DHCP/DNS statics: declared single-threaded
  contract (bounded buffers: worst case is malformed
  wire bytes, never OOB). No lock: no current threat.
- Deliver-side length guard (defense in depth).

## Evidence

`NETSTRESS-OK` (flood last-wins, 400-wave churn,
timeout-late, rebind-generation EBADF, exact
2×100 ping-pong); T21 (+13 shapes, 2000-flood, new
boundary vectors); T14 (+4 seeds, NAK inline).
23/23, CORPUS-OK 7/7 unchanged.

## Consequences

- Concurrent socket lifecycles supported (same
  discipline as FS big lock, Phase H pattern).
- Threads must not exit while parked (audited, no
  enforcement — same posture as FS direct-call rule).
- SMP: lock is bus-correct; per-CPU queues later.
