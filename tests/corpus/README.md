# tests/corpus/ — checked-in fuzz seeds (Phase 0.3-F)

Wire-protocol seeds for the DHCP validator and DNS codec,
replayed by T14 (`test_net.c` corpus block, corpus dir from
argv[1]). Built byte-for-byte by `tools/mkcorpus.py` from the
same constructions as the inline mutants (delete nothing on
drift — regenerate with the script and re-verify).

## DHCP (`dhcp_parse_msg`, XID 0x12345678, chaddr 08:00:27:3C:A1:A1)

- `dhcp-offer.bin` (300B): valid OFFER → OK (msgtype 2,
  yiaddr 10.0.2.15, server 10.0.2.2, mask /24, router
  10.0.2.2, lease 3600).
- `dhcp-xid.bin`: transaction ID flipped → EINVAL.
- `dhcp-magic.bin`: cookie zeroed → EINVAL.
- `dhcp-short.bin` (200B): truncated options → EINVAL.
- `dhcp-chaddr.bin`: client MAC flipped → EINVAL.
- `dhcp-wireaddr.bin`: yiaddr zeroed → EINVAL.

## DNS (`dns_parse`, ID 0x1234, QNAME example.com)

- `dns-ok.bin` (45B): valid A reply → OK (93.184.216.34).
- `dns-id.bin`: ID flipped → EINVAL.
- `dns-nxdomain.bin`: RCODE 3 → ENOENT.
- `dns-trunc.bin` (40B): answer cut → EINVAL.
- `dns-cname.bin`: CNAME-only answer → ENOENT.

## Policy (fuzzing strategy, abridged)

New parser bugs land here first: add the offending input as
a file + its expectation in the replay table (never only as
inline code). PRNG logic fuzz stays inline with fixed seeds
(deterministic); this corpus is the cross-run regression
breadth. See ADR-0070.
