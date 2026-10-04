# ADR-0034: DHCP + UDP + IP completion (dynamic config, datagrams)

Status: accepted (Phase 9c). Date: 2026-10-04.

## Context

Phase 9b proved frames on the wire but with a static config baked
in at compile time. Phase 9c completes the network layer: UDP
datagrams (DHCP's transport), a DHCP client (dynamic self/gw/mask),
and the remaining IP input discipline (fragments, TTL).

## Decision

`kernel/net/net.c`: UDP build/parse/send/recv (pseudo-header
checksum, computed-0 ships as all-ones; RX 0 means absent per RFC
768; broadcast skips ARP, unicast resolves it, off-subnet is
EHOSTUNREACH), DHCP DISCOVER -> OFFER -> REQUEST -> ACK/NAK over
broadcast UDP with bounded retries (2 rounds x 2 tries, ~16s worst;
live server answers in ms), runtime config (`g_self_ip/g_gw_ip/
g_net_mask/g_lease_secs`, init = VBox NAT defaults) applied on
ACK with gateway re-ARP. Every option length is bounds-checked;
XID/chaddr/magic must match; offered IP + mask must be sane
(unicast, non-loopback, on-subnet host bits); unknown options
skip. `net_ip_parse` now refuses fragments (MF/offset; DF passes)
and TTL 0 (no reassembly, ever). `SYS_DHCP` (18, re-acquire) +
`SYS_NETINFO` (19, read config, `do_meminfo` validation pattern);
shell `ip` + `dhcp`. Selftest prints `DHCP-OK
ip=.. mask=.. gw=.. lease=..`, gated in `run-vbox` + `panic-test`.
Host T14: pinned UDP checksums (python oracle), 500-packet
recomputation fuzz, crafted OFFER + 6 rogue mutants, frag/TTL
refusals, no-NIC refusal.

## Evidence

Guest `DHCP-OK ip=10.0.2.15 mask=255.255.255.0 gw=10.0.2.2
lease=86400`, `NET-WIRE-OK`, `NET-OK`; pcap shows DISCOVER ->
ACK -> ARP -> ICMP both ways; `nova test` 14/14.

## Hard lessons

- The magic cookie is decimal 99.130.83.99, not hex 63..: a
  self-consistent build+test pair can both be wrong the same way.
  Pin wire constants against the RFC text, not each other's output
  (the stage-tagged refusal print found it in one boot).
- VBox NAT answers DISCOVER with a full ACK (rapid-commit style),
  not OFFER: XID-matched and fully validated, it commits
  directly; the REQUEST path stays for strict servers.
- Timestamps, not sizes, prove a rebuild (`kernel.bin` is
  padding-rounded); serial.log is harness-truncated per boot.

## Consequences

- Static 10.0.2.15/10.0.2.2/24 remain only as pre-DHCP
  defaults; the runtime config owns routing past acquisition.
- No renewal timers (lease 86400s outlives any session; shell
  `dhcp` re-acquires); no DNS use (option 6 parsed past, not
  stored); UDP is kernel-internal (user sockets -> later phase).
- Next: Phase 10 security hardening.
