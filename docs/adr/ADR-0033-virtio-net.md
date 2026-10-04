# ADR-0033: virtio-net wire proof (ARP + gateway ping over NAT)

Status: accepted (Phase 9b). Date: 2026-10-04.

## Context

Phase 9a proved the packet path over loopback with the NIC absent.
Phase 9b puts frames on a real wire: virtio-net driver + ARP +
gateway ping, with the recipe switching to NAT + virtio NIC
(resource budget unchanged: 2 CPU / 1024 MB).

## Decision

`kernel/drivers/virtio_net.c`: modern-only transport on the
transitional ID (PCI caps walk to COMMON/NOTIFY/ISR/DEVICE on BAR2,
mapped via `vmm_map_mmio`; kernel-CR3 window for every register
touch), 1.0 handshake (ACK+DRIVER, MAC + MRG_RXBUF + VERSION_1,
DRIVER_OK), two 64-entry queues (desc+avail in one PMM frame, used
in the next), 8 posted RX pages, single-descriptor TX through a
bounce frame. `kernel/net/net.c`: Ethernet + ARP (`arp_resolve`,
broadcast request, directed-reply match, gateway cache) +
`ping_wire` (ARP then ICMP echo, 200/300-tick RX waits);
`net_selftest` asserts ARP + wire ping and prints `NET-WIRE-OK
gw=<mac>`. Recipe: `--nic1 nat --nictype1 virtio`.

## Evidence

Guest `VIRTIO-OK mac=08:00:27:3c:a1:a1`, `NET-WIRE-OK
gw=52:55:0a:00:02:02`, `NET-OK`; pcap shows ARP request (42B),
ARP reply, ICMP request + reply; `nova test` 14/14.

## Hard lessons

- QueueSize must be written back (spec 4.1.4.3): the queue reads
  as size 0 until the driver writes it, so Enable alone consumes
  nothing (used ring frozen, no ISR). Symptom-perfect silent stall.
- virtq_desc is addr-u64/len-u32/flags-u16/next-u16: packing len
  at offset 4 shifts every field and hands the device a wild
  64-bit address. Width-correct MMIO accessors throughout.
- The device (VBox) consumes the first 12 chain bytes as the TX
  header and prepends a 12B RX header (num_bufs verified = 1):
  with only MAC negotiated the wire frame loses its first 2
  bytes. Negotiate MRG_RXBUF and speak 12B headers both ways.
- Stale-binary discipline: kernel.bin size is padding-rounded,
  so timestamps (not sizes) prove a rebuild; serial.log is
  harness-truncated per boot, VBox.log timestamps are VM-uptime.

## Consequences

- Static 10.0.2.15/10.0.2.2 (no DHCP yet); single in-flight TX
  discipline with `-EAGAIN` on full; RX merges capped at one
  descriptor per frame (fits: 1526B < 4K page).
- Next: DHCP/UDP/IP completion, then Phase 10 hardening.
