# NOVA OS v0.8.0 — Networking and Internet Tests

Honest map of what the network does, per capability.
Evidence kinds: CODE (source-read) · LIVE (this
campaign, screenshots net1–net6) · SUITE (gates).

## 1. Virtual device detection — IMPLEMENTED+TESTED

Virtio-net transitional (`1AF4:1000`) on PCI, INTx
routed and counted (`virtio_net.c`, `irq.c`;
NET-OK gate asserts `virtio_irq_count() > 0` —
interrupts genuinely arrive). LIVE: every boot.

## 2. Link/device init — IMPLEMENTED+TESTED

Queue setup, MAC from device config, RX posting
(driver selftests + VIRTIO-OK). No link-state
API exists (no carrier concept; NAT is always
"up"). Userspace cannot query link state —
documented gap, not a failure.

## 3. IP configuration — IMPLEMENTED+TESTED

Static default (self 10.0.2.15, gw 10.0.2.2,
mask /24) plus a real DHCP client (option parse,
sanity checks; DHCP-OK in-guest). `ip` prints the
live values; `dhcp` re-runs negotiation. LIVE:
`ip` output observed (prior sessions).

## 4. ARP + local communication — IMPLEMENTED+TESTED

ARP resolve with bounded retries (net.c); gateway
ping proven LIVE this campaign:
`ping` → `reply seq=1 time=0`.

## 5. Outbound IP / general routing — NOT IMPLEMENTED

`net_ping`/`net_udp_send` refuse off-subnet
destinations with EHOSTUNREACH ("no routing yet",
net.c:256,273-275). LIVE: `ping 8.8.8.8` →
`no route to host`. This is a designed boundary,
not a NAT misconfiguration. Anything beyond the
virtual /24 is unreachable by data plane.

## 6. ICMP echo — PARTIAL (gateway only)

Works to the gateway and loopback (LIVE). Beyond
that: refused per §5. No traceroute concept.

## 7. DNS resolution — IMPLEMENTED+TESTED (via NAT proxy)

A-record resolver over UDP to the NAT DNS proxy
(10.0.2.3, on-subnet so routing allows it).
LIVE this campaign: `nslookup example.com` →
`example.com has 104.20.23.154` (real public
answer through the proxy). This is the extent of
NOVA's "internet": names resolve; connections do
not (no TCP — see §8).

## 8. TCP connections — NOT IMPLEMENTED

Grep-verified: no TCP code exists anywhere in
tree. No sockets beyond UDP, no connect/listen
APIs.

## 9. HTTP — NOT IMPLEMENTED (impossible without §8)

## 10. HTTPS/TLS — NOT IMPLEMENTED (no TCP, no crypto
## library, no certificates; grep-verified absent)

## 11. Errors and timeouts — IMPLEMENTED+TESTED

`no route to host` (off-subnet), bounded ARP/DHCP/
DNS retries with errno returns, socket pool
EMFILE + waiter EBUSY matrix (SOCKBLK-OK),
queue-flood drop-newest (NETSTRESS-OK). LIVE:
usage errors and ENOENT-style refusals observed
on screen this campaign.

## Bottom line for testers

"Internet" on NOVA v0.8.0 = pinging the gateway
plus resolving public DNS names. There is no web,
no downloads, no SSH, no connections of any kind.
Any guide claiming otherwise is wrong; this one
does not. Bridged networking is unnecessary and
unasked-for: NAT already answers every question
this stack can pose.
