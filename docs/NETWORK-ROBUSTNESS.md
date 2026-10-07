# NOVA Network Robustness (v0.5 Phase I — Outcome B, minimum hardening)

UDP/ARP/DHCP/DNS over virtio-net (transitional) + loopback.
No TCP, no reassembly (refused by design), no routing.

## 1. Architecture

RX: device ring (8×1526B) → tick pump (net_rx_lock) →
socket divert (sock lock) or pending queue (4×1600B,
drop-newest) → filtered recv. TX: builders (stack/static
buffers) → ARP resolve → virtio TX (EAGAIN when full).
Loopback short-circuits to socket slots (no wire).
ISR acks + counts only; all parsing in thread/tick
context, never IRQ.

## 2. Ownership

| Object | Owner | Rule |
|---|---|---|
| RX descriptors/buffers | device, then pump on drain | re-post after copy |
| pending queue entries | net_rx_lock | FIFO, drop-newest |
| socket pool/slots/waiters | sock lock (+ park-cli) | §11 |
| TX frame/statics | sending thread | single-threaded use |
| DHCP/DNS statics | calling thread | single-threaded use |
| ARP gw cache | resolver (single-threaded) | validated writes only |

## 3. Packet validation rules

Bounds before dereference, everywhere: eth ≥14 + MAC
filter; IP version/IHL/total/checksum/frag-refuse/TTL;
UDP ulen in [8, len] + checksum-or-absent; DHCP fixed
fields + XID/chaddr/magic + option-walk caps; DNS
header/question/answer counts + 8-jump name cap.
Corrupt input → DROP/ERROR, never panic/tear.

## 4. Resource limits

Pool 8 sockets (EMFILE) · ports first-bind-wins ·
single 1472B slot/socket (overwrite) · queue 4
(drop-newest) · TX one frame (EAGAIN) · RX ring 8 bufs ·
no reassembly state (refused) · RX waits tick-bounded ·
options/names length-capped · 64KB file cap (FS, not
net) · 8192B syscall range (unchanged).

## 5. Queue limits

See §4. Full queue: deterministic drop-newest, indices
intact, wakeups preserved (proven: 6-frame flood keeps
first 4 in order).

## 6. Fragment limits

None exist: MF/offset/frag-flagged packets refuse
(EINVAL) at parse. No contexts, no timeouts, no
eviction needed. Overlapping/out-of-order/duplicates
are moot (nothing reassembles). Corpus pins refusal.

## 7. Socket lifecycle

create→bind→send/recv→block→wake→close→cleanup, all
under the pool lock. Exhaustion→EMFILE; dup→EADDRINUSE;
bad id→EBADF; waiter exists→EBUSY; closed waiter→EBADF
(validity first, generation second). 400-wave churn
proven clean with exact drain.

## 8. Timeout behavior

Tick-countdown waits (rx_wait, sock deadline, DHCP/DNS
rounds). Expiry revalidates (racing delivery wins;
closed/rebound → EBADF; silence → EAGAIN/ETIMEDOUT).
Frozen ticks + nonzero timeout would spin (accepted:
ticks always advance in guest; hosts use timeout-0).

## 9. Allocation failure

kmalloc sites (materialize ≤64KB, nodes, frames):
fail → ENOSPC, prior state untouched (grow-before-
commit ordering). Pool full → EMFILE. TX full →
EAGAIN→EIO. No partial construction (single-exit
wrappers; verified by churn + flood).

## 10. Interrupt-context rules

ISR: ack + count only (no lock, no alloc, no parse, no
blind sti). Pump runs in tick (IF=0) with rx_lock, then
sock lock (rx→sock: the only nesting). Threads take
sock lock alone. Sleep never held (park is cli-only).

## 11. Locking model

`g_sock_lock` (irqsave, owner=current, Phase I): pool
slots, waiter, counters. Park path keeps its verified
cli check-and-park (revalidation converges; a delivery
racing park resolves via has-recheck at expiry).
TX/DHCP/DNS statics: single-threaded contract (bounded
buffers: worst case malformed wire bytes, never OOB).

## 12. User/kernel boundary

All net syscalls validate pointers first (T8 +
Phase D patterns); lengths capped (1472/64KB/8192);
payload validated before dereference (huge-proof).
No change in Phase I.

## 13. Adversarial corpus

`tests/corpus/{udp,dhcp,dns}/` (mkcorpus.py-generated,
deterministic): 13 wire shapes + 15 parser seeds, each
with recorded ACCEPT/DROP/ERROR. Bug→seed rule
(FUZZING.md) applies.

## 14. Stress methodology

Host: 2000-frame demux flood + 2000-op discipline fuzz
(frozen ticks, timeout-0). Guest: 300-send last-wins,
400-wave churn, timeout-late, rebind-gen, 2×100
ping-pong pair (exact bytes). Structural asserts only.

## 15. Known limitations

- TX/DHCP/DNS statics: concurrent threads would
  interleave packet bytes (bounded, no OOB). Contract:
  single-threaded net use (shell is; no kernel thread
  sends concurrently today).
- Threads must not exit while parked on a socket
  (waiter would dangle; all parking paths return
  before exit — audited, no enforcement).
- Trailing-garbage past IP-total is tolerated (parsed
  length governs; UDP-unreliable, harmless).
- Zero UDP checksum = unchecked (RFC 768 choice).
- ARP trusts first valid reply (no crypto possible).
- Frozen ticks + nonzero timeout spins (guest ticks
  always advance; hosts probe with 0).

## 16. Accepted limitations

Same list as §15 (deliberate: fixing each would cost a
TX serializer / waiter reaper / strict-total mode with
no current threat — all single-threaded in practice).

## 17. Future work

TX serializer if a second sender ever exists; waiter
lifetime hook if parked-exit becomes reachable; strict
total-length clamping if a consumer needs it; per-CPU
net state at SMP (lock already bus-correct).
