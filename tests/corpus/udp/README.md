# tests/corpus/udp/ — wire-shape seeds

Full Ethernet frames (built by `tools/mkcorpus.py`),
replayed by T21 through the real demux. All drop shapes
expect 0 (no crash, no hang, no delivery); `udp-hit.bin`
(dport 5200) must deliver 2 bytes (validates the
generator's checksums end to end — drop-only shapes would
pass vacuously on broken checksums).

Shapes: `udp-ok` (valid, unbound port), `udp-hit`
(bound → delivers), `ip-badver`, `ip-mf` (fragments
refused — no reassembly, ever), `ip-ttl0`, `ip-trunc`,
`ip-badproto` (TCP=6, not UDP), `udp-short` (ulen<8),
`udp-badcksum`, `udp-huge` (ulen exceeds frame; jumbo
only — legal MTUs max out at exactly 1472 payload).
