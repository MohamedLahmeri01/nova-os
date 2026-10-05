# NOVA OS — Debugging guide (Phase 11b)

All recipes below are the ones the ADRs paid for. Start with the
serial log; escalate only as far as the symptom demands.

## 1. Serial log (`build/serial.log`)

The harness truncates it per boot: the tail is always the current
boot, never history. Markers are the contract (`VIRTIO-OK`,
`NET-WIRE-OK`, `CAPS-OK`, ...): a boot that stops after marker X
failed in the next subsystem. `*-FAIL` lines name the exact check;
`PANIC: <reason>` names the selftest that gave up.

## 2. Panic format

```text
PANIC: net-selftest-fail
cpu=0
eip=0x... eflags=...
eax=... ebx=... ...
cr0=... cr2=... cr3=...
STACK:
  #0 eip=... ebp=...
END-PANIC-HALT
```

`cr2` is the fault address on #PF; the EBP chain is a stack walk
(stops at corrupt frames by design). To map a faulting `eip` to
code, disassemble the linked PE (runtime VAs match directly):

```text
objdump -d --start-address 0x<eip-32> --stop-address 0x<eip+32> build/kernel.pe
```

(mingw objdump sits next to gcc; see ADR-0035 for the full story:
a fault *inside* `sched_enter` at `movl $1,0x8(%eax)` with
`eax=0` meant a NULL thread argument, not a broken scheduler.)
`TRAP vec=14 err=0x3 pf-addr=0x8` reads as: protection fault on a
*write* to the null-guard page (offset 8 of NULL).

## 3. Marker gates

`tools/nova.py` holds two lists: boot gates (`run-vbox`) and
panic gates (`panic-test`). Markers printed by *spawned threads*
(`HI-OK`, `SDK-OK`) belong in boot gates only: the fault image
panics before they are ever scheduled (ADR-0039 caught this via
a red T6). When adding a marker, decide which list it joins.

## 4. Wire proof (`build/nic.pcap`)

The NAT recipe captures every frame (44B pcap header + records).
Decode with stdlib python (`struct.unpack('<IIII', ...)` for the
per-packet header, then Ethernet/IP/UDP/BOOTP by offsets). The
reference decodes live in the phase history: ARP request/reply
(ADR-0033), DHCP DISCOVER vs server-ACK with rapid-commit
acceptance (ADR-0034), shifted-by-2 frames meaning a header-size
mismatch (ADR-0033: device consumed 12 header bytes, not 10).

## 5. Hypervisor logs (`vms/<name>/Logs/VBox.log`)

Timestamps are VM-uptime, not wall clock. Assertion lines name
the device model invariant violated (e.g. DevATA's bus-master
direction check: reads must carry the FROMDEV bit -- see
ADR-0030). A clean guest panic leaves no assertion; an abort
with one means the guest broke a device contract.

## 6. Stale-binary discipline

`kernel.bin` size is padding-rounded: identical sizes prove
nothing. Trust mtimes (source < object < `kernel.bin`) after
every edit. Same for `init.bin` et al.

## 7. Fault image (`nova panic-test`)

Builds with `-DNOVA_FAULT_TEST` and takes the deliberate `#UD`
after `INIT-OK`. Use it to verify panic *diagnostics* (trap
vector, register dump, stack walk), not subsystem proofs.

## 8. Host tests first (T7–T17)

Pure logic (checksums, parsers, caps, rlimits, fs walks, ABI
freshness) runs on the host in milliseconds. A guest-only
failure with passing host tests means the bug is in
wiring/dispatch/timing, not the algorithm -- bisect there.

## 9. Pattern catalog (symptom -> cause)

| Symptom | Ask first | ADR |
|---|---|---|
| Used ring frozen, no ISR, status OK | QueueSize written back? Descriptor layout (addr-u64@0)? | 0033 |
| Wire frame shifted/truncated | Header-size agreement (10 vs 12B)? | 0033 |
| DHCP ignored despite valid packets | Magic cookie decimal 99.130.83.99? ACK-to-DISCOVER? | 0034 |
| VBox abort in DevATA | Bus-master direction bit on reads? | 0030 |
| Fault at end of a passing test | Saved-NULL restore (`sched_enter`)? | 0035 |
| EEXIST where EACCES expected (or vice versa) | Fail-closed ordering: perm before existence? | 0037 |
| Missing marker in fault image only | Spawned-thread marker in panic gates? | 0039 |
| Suite red after syscall addition | `docs/ABI.md` regenerated (`nova abi`)? | 0039 |
