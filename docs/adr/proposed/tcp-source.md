# Proposed ADR — TCP/IP source: adopt, never hand-roll (number assigned on ratification)

Status: PROPOSED — NOT RATIFIED. Date: 2026-10-09.

## Context

v0.5 has UDP/ARP/DHCP/DNS-client with proven
parsers. TCP needs retransmission, congestion,
SACK, TIME-WAIT, attack-resistant state machine —
a multi-year specialty. Options: (a) adopt an
audited embedded stack (license + maintenance +
portability review; isolate behind the NIC/socket
interfaces); (b) hand-roll (reject: the review
depth does not exist in-house); (c) userland TCP
over raw frames (defer: needs the driver/DMA
story settled first).

## Trade-offs

(a) inherits upstream CVEs (SBOM + patch process
required) but starts from tested state machines.
Evaluate at minimum: license compatibility with
Apache-2.0 distribution, 32/64 cleanliness,
no-POSIX assumptions, memory-ownership model that
fits NOVA's snapshot discipline, and a test suite
we can run in-guest.

## Prerequisites / evidence to decide

M1 SBOM process; M2 socket quotas (a TCP stack
without memory bounds is a DoS engine); candidate
scorecard published before any code lands.
