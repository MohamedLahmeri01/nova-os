# NOVA OS — Foundation Contracts (Phase M: M0 + M1)

Normative for all post-v0.5 work. Each contract states
owner, rules, failure behavior, and the test that proves
compliance. Nothing here changes code; contracts bind
future phases. Status labels: RATIFIED (existing ADR or
v0.5 proof) · PROPOSED (needs human approval).

## A. Threat model (pointer + deltas)

Canonical model: docs/THREAT-MODEL.md (8 profiles
P1–P8, 6 boundaries, 15-row control matrix, 5 load-
bearing assumptions). Phase M adds three required
extensions (PROPOSED until reviewed):

1. P6-physical gains per-board refine: each named
   board documents its tamper/cold-boot/flash-
   extraction posture; "physical out of scope" is
   no longer blanket once hardware is named (M5).
2. Fleet-scale attacker (new P9): one compromised
   device attacking update server / peers; requires
   per-device keys, revocation, and staged-rollout
   blast-radius limits (M6).
3. Registry of residual risks becomes a living
   appendix: every ACCEPTED entry carries an expiry
   milestone (revisit, not forever).

Remote, compromised-app, malicious-driver,
corrupted-storage, interrupted-update, supply-
chain, and exhaustion attackers are already
covered (P1–P5, P7–P8). No change needed there.

## B. ABI stability contract (RATIFIED base, PROPOSED policy)

Base facts (RATIFIED by v0.5 evidence): 38 syscalls
0..37 via INT 0x80 (EAX num, EBX/ECX/EDX args,
EAX 0/-errno); SDK mirrors in sdk/include/nova/;
structs fixed-width (nova_trace_rec 20B,
nova_winstat, nova_runtime/stats); error codes
Linux-compatible negatives; zero-length touches
nothing; faults→errno never panic.
Policy (PROPOSED — needs approval with
proposed ADR abi-policy):
1. Numbers and struct layouts frozen at v1.
2. All change additive: new calls append only;
   new struct fields append + version bump.
3. Fixed-width discipline enforced by lint (no
   `long`, no pointers, explicit padding).
4. Every ABI-touching change ships: validation
   rules, error codes, host + guest tests, ABI-
   compat corpus run, CHANGELOG migration note.
5. Deprecation: two-release warning minimum; no
   silent removal, ever.
6. 32/64 transparency rules activate at first
   64-bit backend (audit every crossing struct).

## C. Locking and concurrency contract (RATIFIED base, PROPOSED extension)

Base (RATIFIED: docs/LOCKING.md + L1/L2 fixes):
spinlock + irqsave pair, non-reentrant by design;
sleep forbidden while holding any spinlock or
inside any IF-held section; park-then-sleep
mandatory; no lock held across yield/switch/
sleep/exit; single nesting rx→sock documented;
IRQ handlers take net_rx_lock only, never sleep/
allocate; saved IF words are caller stack state,
never globals; refcounts move under owner
discipline only.
SMP extension (PROPOSED — binds M2):
1. Today's BSP-only evidence proves NOTHING
   about SMP safety (stated explicitly; any
   reviewer claiming otherwise fails review).
2. First SMP design must name: per-CPU data,
   runqueue + sleep-list lock, shootdown
   mechanism, fence placements for ARM-weak
   ordering (x86-TSO assumptions re-audited).
3. Contention proofs at 2+ CPUs (SYNC-OK pattern)
   gate every lock's SMP debut; no lock ships
   multicore without a contention test.
4. Scheduler interactions: wakeup paths re-
   audited under the new lock; tick-handler
   reentrancy rules rewritten, not inherited.
5. Driver lifecycle: probe/remove serialized by
   the driver framework (M4), never by IF luck.

## D. DMA and device-trust contract (PROPOSED)

1. Assumption default: every DMA-capable device
   is hostile until a per-device policy says
   otherwise (extends W5 from note to rule).
2. Buffer ownership: DMA buffers are kernel-owned,
   pinned for the transfer, validated (bounds +
   alignment) before programming, unmapped/
   released on completion AND on error/timeout/
   removal paths (leak = test failure).
3. Cache coherency: non-coherent DMA requires
   explicit invalidate/clean at hand-off points,
   documented per driver; coherent (x86) still
   states its assumption in review.
4. IOMMU: required where the board provides it
   (policy: identity-map nothing by default;
   least-privilege domains per device); where
   absent, the board doc states compensating
   controls (bounded descriptors, short windows,
   no userspace DMA buffers — today's posture,
   kept explicitly).
5. Removal/failure: surprise removal must not
   wedge the kernel (timeout → teardown →
   diagnostics bundle); hang detection owned by
   the driver framework (M4/M5), not ad hoc.

## E. Security disclosure and maintenance (PROPOSED — process missing)

`SECURITY.md` does not exist (THREAT-MODEL §5
assumption 5). Required contents when created
(do NOT invent contact/publish unapproved — the
file ships with placeholders for human-filled
fields marked [HUMAN]):
1. Scope: which trees/branches/releases are
   supported (ties to lifecycle proposal).
2. Report intake: [HUMAN: contact address/PGP],
   what a good report contains, response-time
   targets (e.g. acknowledge 2 business days —
   example, not policy).
3. Triage: severity rubric (CVSS-ish + NOVA
   exploitability notes: ring0? remote? default
   config?), who decides.
4. Coordinated disclosure: embargo defaults,
   researcher credit, no-exploitable-detail-
   before-fix rule (already in THREAT-MODEL).
5. Patch flow: `security/*` branch convention,
   verification (regression test named), advisory
   text template, backport rules per support
   window.
6. Dependency vulns: SBOM-driven watch list,
   upgrade-or-patch decision log, emergency
   rollback via OTA (M6).
7. Emergency path: who can authorize an out-of-
   band release + rollback, and the drill that
   proves it (M7 runs the drill for real).
Until SECURITY.md exists with [HUMAN] fields
filled and the first drill run, support-lifecycle
claims are unenforceable — promises with no one
home. The M7 exit gate requires the drill, not
the document.

## F. Portability contract inventory (PROPOSED — binds M1/M5)

One row per contract: owner · interface shape ·
failure behavior · test strategy. All PROPOSED
until the portability-boundaries ADR is ratified.

1. CPU/atomics — owner arch backend. Test-and-set,
   fences with documented strength (x86-TSO vs
   ARM-weak called out per function). Failure =
   build error on missing primitive (never silent
   fallback). Test: host stub + spinlock proofs.
2. MMU/pagetables — owner arch backend. Map/unmap/
   protect with explicit permission words incl. NX
   bit handling per ISA. Failure: refuse (never
   map RWX). Test: permission-grid audit per arch.
3. Exception entry — owner arch backend. Frame
   layout contract (regs/err/vec/tick-equiv);
   portable code reads frames, never raw stacks.
   Failure: halt with reason. Test: fault battery
   per arch (UD/GP/PF equivalents).
4. Context switch — owner arch backend. Callee-
   saved discipline + canary placement. Failure:
   canary panic. Test: ping-pong + canary checks.
5. Memory barriers — owner arch backend, but every
   portable USE names its requirement (acquire/
   release/full). Failure: lint flags undocumented
   barrier. Test: ordering litmus under preemption.
6. Boot protocol — owner boot/plat producers.
   Versioned NOVOSBOOT struct only; kernel never
   parses firmware tables. Failure: bad magic/
   version → halt with code. Test: malformed-
   struct corpus.
7. Physical memory map — owner plat producer.
   Regions with type/overlap rules; ARENA_MIN-style
   invariants per board. Failure: halt, no guessing.
   Test: overlap/gap corpus.
8. Interrupt controllers — owner plat backend.
   Enable/ack/EOI/mask with per-IRQ state.
   Failure: spurious storm → counted + masked +
   diagnostics (never silent). Test: storm drills.
9. Timers/clocks — owner plat backend. Tick source
   + one-shot + wall (when HW has it); tick-native
   consumers only (TIMEBASE rule carried over).
   Failure: timer-dead panic (today's rule, kept).
   Test: 20-tick prove + wrap tests per source.
10. Firmware/discovery — owner plat producers.
    UEFI/DT/E820 normalized to NOVOSBOOT; PCI enum
    feeds driver registry. Failure: unknown board
    → halt naming the board. Test: fixture tables.
11. DMA/IOMMU — owner plat + driver framework.
    Map/unmap/teardown per contract §D; least-
    privilege domains where HW allows. Failure:
    refuse transfer. Test: descriptor-lie fuzz.
12. Power management — owner plat + drivers.
    States as policy with per-driver hooks; always
    a working "no-op" state. Failure: stay awake
    + log (never half-sleep). Test: transition
    cycles + wakeup proofs per board.
