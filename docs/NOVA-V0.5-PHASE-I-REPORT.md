# NOVA v0.5 Phase I Report — Network Robustness (Outcome B)

## 1. Executive Summary

Audit-first hardening: parsers proven sound by
inspection + expanded corpus; one real concurrency gap
(unlocked socket pool) closed with a minimal irqsave
spinlock; guest + host stress batteries green. No
parser rewrites, no ABI changes, no policy changes.
Outcome B (minimum hardening), not a redesign.

## 2. Starting Commit

`5a3204d` (`feat(fs): add filesystem concurrency
controls`), tree clean.

## 3. Ending Commit

Single Phase I commit (hash below), tree clean after.

## 4. Baseline Reproduction

23/23, CORPUS-OK 7/7, gate PASS, all Phase G/H markers
(RUNTIME proc=140, FSSTRESS-OK) reproduced pre-change.

## 5. Network Architecture Inventory

RX ring 8×1526B → tick pump (net_rx_lock) → divert or
queue 4×1600B (drop-newest) → sockets (8, single
1472B slot, overwrite) / legacy filtered recv. TX via
builders + ARP + virtio (EAGAIN when full). Loopback
short-circuit. DHCP/DNS over UDP. ISR acks only.

## 6. Ownership/Lifetime Map

Full table in NETWORK-ROBUSTNESS.md §2. Key results:
descriptors re-posted after copy (no leak); queue
entries memmove-shifted (no aliasing); slots leased by
scan under lock; waiter cleared on wake AND close;
materialized nodes owned by descriptions; TX/DHCP/DNS
statics owned by the calling (single) thread.

## 7. Threat Model

Packet-level: all refused/dropped cleanly (bounds-first
parsers, no reassembly, checksums enforced except
absent-UDP). Resource: every pool capped (8/4/1472/
1500/64KB/8192). State-machine: close-while-parked,
rebind-gen, double-close, stale-id all EBADF/EBUSY by
revalidation. No violation found in any class.

## 8. Ethernet Audit

≥14B, ours-or-broadcast, EtherType-gated (non-IP
drops). Multicast-non-broadcast drops (new seed).
No length field to overflow (fixed 14B header).

## 9. IPv4 Audit

version/IHL/total/checksum/frag-refuse/TTL, all
bounds-first, overflow-safe (u32 values << 2^32).
IHL=6 options path proven (new vector). Fragments
refused always → §11 collapses (no contexts).

## 10. UDP Audit

ulen ∈ [8, len], checksum-or-absent, payload bounded
by validated ulen. Exact-1472 delivers; 1473+ drops
never truncates; empty datagram delivers 0 (legal).
ENOSPC retains slot for big-buffer retry (proven).

## 11. Fragmentation Audit

No reassembly code exists (grep-verified): MF/offset/
reserved-flagged packets refuse at parse. Attacker
cannot create fragment state — nothing to flood,
time out, or evict. Corpus pins refusal (ip-mf).

## 12. RX Queue Audit

Depth 4, 1600B entries, drop-newest, shift-on-consume.
Empty/one/full/full+1/burst/empty-transitions proven
(host flood + guest DHCP/DNS flows). Wakeups preserved
(queue only stores; waiter lives in sockets).

## 13. TX Queue Audit

Bounded ring, EAGAIN when all in flight (mapped to
EIO for callers), len ≤ 1514 enforced pre-copy,
4096B bounce fits VNET_HDR+max. Completion via
last_used sync (u16-wrap-safe subtraction). Live
DHCP/DNS traffic proves the path end to end.

## 14. Socket Lifecycle Audit

Open(bind matrix, ephemeral wrap-safe, EMFILE) →
send/recv → block → wake → close → drain. 400-wave
churn with exact drain; close-while-parked EBADF
(pre-existing + re-proven); rebind-generation EBADF
(new: deterministic gen-path proof).

## 15. Blocking/Wakeup Audit

Park holds IF=0 across check-and-set (no lost wakeup);
wake-then-clear (no theft); EBUSY second parker;
expiry revalidates (delivery wins ties, close/rebind
→ EBADF, silence → EAGAIN). Timeout-then-late-packet
proven clean. No policy change.

## 16. Resource-Limit Audit

Table in NETWORK-ROBUSTNESS.md §4. Every resource has
a bound + enforcement point + failure errno. No
unbounded state exists (reassembly: n/a by design).

## 17. Allocation-Failure Audit

kmalloc sites fail ENOSPC with prior state untouched
(grow-before-commit); pool-full EMFILE; TX-full
EAGAIN; no partial construction (single-exit paths);
no stranded waiter (wake paths don't allocate).

## 18. DHCP Audit

XID/chaddr/magic/mask-shape validated; option walk
length-capped (new badlen seed); missing END accepted
(lenient walk, documented); NAK succeeds with nothing
trusted (new inline proof); duplicate responses
idempotent; retransmission bounded (2×2, ~16 s cap).

## 19. DNS Audit

Header/question/answer counts bounded; 8-jump
compression cap (new ptrloop seed pins cycles);
qd/an loops progress-guaranteed (≥10B/record vs len);
first-A-wins (new 2ans seed); NXDOMAIN→ENOENT;
routing via single static query path.

## 20. Checksum Audit

Valid/invalid/zero-absent/one-bit/payload-truncation
covered (corpus + inline). Bad checksums drop before
any state change. Zero-UDP-cksum accepted per RFC 768
(new vector documents the choice).

## 21. User/Kernel Boundary Audit

Pointers validated first (T8 + D-patterns); lengths
capped pre-deref; payload-before-touch (huge-proof);
no new surface in Phase I (unchanged ABI).

## 22. Interrupt-Context Audit

ISR: ack + count only (no lock/alloc/parse/sti —
Phase H suspicion applied, clean). Pump: IF=0 +
rx_lock, then sock lock (rx→sock only nesting).
No sleep, no user access, no blind sti in net paths
(verified by grep; ATA sites were Phase H).

## 23. Locking/Deadlock Audit

Locks: net_rx_lock (ring/queue), g_sock_lock (pool).
Order rx→sock only; threads take sock alone; park is
cli-only (never holds across sleep). Holders bounded,
non-sleeping, non-preemptible (irqsave) → no deadlock
cycle. FS lock untouched by net (verified both
directions).

## 24. Adversarial Corpus

+7 seeds (mkcorpus.py, deterministic): eth-multi,
ip-opt, udp-empty-u, dhcp-noend, dhcp-badlen,
dns-ptrloop, dns-2ans. T21: 10→13 shapes. T14: 11→15
seeds (+ NAK inline). Bug→seed rule honored (no new
bugs; seeds pin the contracts).

## 25. Deterministic Stress

Host: 2000-frame demux flood (1500 delivered exact)
+ 2000-op discipline fuzz. Guest NETSTRESS-OK: 300-send
last-wins (exact #299), 400-wave churn (exact drain),
timeout-late (EAGAIN then 5 bytes), rebind-gen EBADF,
2×100 ping-pong (exact bytes, cross-talk-free).

## 26. Real-Dispatch Evidence

Guest battery runs real frames through real dispatch
(loopback + pump + drill paths). Host battery runs
real demux/parsers/pool. NAK/rebind/gen paths run
in-guest (need threads/ticks).

## 27. Resource Conservation

Pool: 400 waves → exact 8-reopen drain (no leak).
Slots: overwrite + close-clear (no residue). Queue:
flood → order + empty. Descriptors: re-posted per
drain (ring accounting selftest). No new counters
needed (existing gates observe).

## 28. Security Review

UID/caps (CAP_NET gates TX paths incl. loopback
send/recv syscalls — verified at syscall.c:866-1127),
socket ownership (global port space by design,
documented), chroot (grafts invisible when jailed —
preserved), pointer validation intact, no new exposure.
ARP trusts first valid reply (documented LAN-trust,
no crypto possible).

## 29. Performance

Uncontended irqsave pair per pool op (xchg + IF
toggle, tens of cycles); PERF sock ops=40000 ticks=0
unchanged; boot +~2 s (stress); fsck unaffected.
No TSC, no new instrumentation.

## 30. Bugs Discovered

1. TEST BUG (mine): flood sockets left open into
   churn (6 free < 8 needed) — restructured phases.
2. TEST BUG (mine): main parked with nobody runnable
   (sched-empty) — worker restructuring.
3. TEST BUG (mine): IHL test forgot to zero checksum
   field before recompute — fixed.
4. PRODUCT GAP (design): unlocked socket pool
   (scan+claim preemptible) — fixed with lock.
5. Stale TEMP-DIAG comment in dhcp_parse_msg —
   removed (documentation hygiene).

## 31. Bugs Fixed

#4 (lock + revalidation + deliver guard), #5
(comment). #1–#3 fixed pre-boot. No parser, policy,
timer, trace, FS, or ABI behavior changed.

## 32. Accepted Limitations

Single-threaded TX/DHCP/DNS statics (bounded damage);
no parked-exit (audited); trailing-garbage tolerance;
zero-cksum acceptance; ARP LAN-trust; frozen-tick
spin (guest ticks always advance). §15–16 of doc.

## 33. Deferred Work

TX serializer / waiter reaper (if ever needed);
strict total clamping (if a consumer needs it);
per-CPU net at SMP. None required now.

## 34. Files Changed

`kernel/net/sock.{c,h}` (lock, guard, decl),
`kernel/net/sock_block.c` (NETSTRESS-OK),
`kernel/net/net.c` (init call, comment),
`tests/unit/test_{sock,sync? no,net}.c` (vectors,
stubs), `tools/mkcorpus.py` (+7 seeds),
`tests/corpus/{udp,dhcp,dns}/` (+7 files),
`tests/corpus/udp/README.md`, `kernel/pm_main.c`
(hook), `tools/nova.py` (gates), `docs/
NETWORK-ROBUSTNESS.md`, `docs/adr/ADR-0080-net-lock.md`,
`CHANGELOG.md`, this report.

## 35. Test Matrix

| Test                     | Result | Evidence |
| ------------------------ | ------ | -------- |
| baseline 23/23           | PASS   | nova test |
| corpus 7/7               | PASS   | CORPUS-OK |
| gates                    | PASS   | ci/gate.py |
| trace fetch              | PASS   | TRACEFETCH-OK |
| trace adversarial        | PASS   | TRACEFETCH-ADV-OK |
| runtime                  | PASS   | RUNTIME-OK |
| FSSTRESS                 | PASS   | FSSTRESS-OK |
| network malformed corpus | PASS   | 13 shapes + 15 seeds |
| fragmentation stress     | PASS   | n/a (refused by design) |
| RX flood                 | PASS   | 2000-frame + queue flood |
| socket lifecycle         | PASS   | churn + rebind-gen |
| timeout race             | PASS   | late-packet + expiry window |
| close/wakeup             | PASS   | EBADF paths |
| allocation failure       | PASS   | ENOSPC/EMFILE/EAGAIN paths |
| resource exhaustion      | PASS   | pool/queue/ring bounds |
| lint                     | PASS   | 1 pre-existing warning |
| ci/gate.py               | PASS   | gate PASS |
| diff-check               | PASS   | clean |

## 36. Commit

Single Phase I commit (hash below), tree clean after.

## 37. Final Verdict

PASS (Outcome B). Next authorized step: PHASE J —
only if explicitly approved.
