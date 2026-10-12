# NOVA OS — Production Readiness Matrix (post-v0.5 audit)

Scope: what v0.5 demonstrably is, against what production
needs. Status scale: VERIFIED (reproducible evidence in
scope) · PARTIAL (subset works, limits named) ·
ABSENT (not implemented) · UNVERIFIED (cannot confirm
from available evidence). A design doc is never evidence.

Baseline: HEAD `0b5da64` (v0.5 Phase L `59ea07b` + two
authorized commits: fbcon CLI console, CLI manual).
Validated envelope: VirtualBox 7.2.6, 2 CPU / 1024 MB,
NAT virtio-net, IDE disk. 23/23 + CORPUS-OK 7/7.

## Boot / platform

| Capability | Status | Evidence / note | Target |
| BIOS boot (custom stage1, E820, A20, mode13h) | VERIFIED | boot/stage1.S, every VM boot | M0 |
| Versioned boot-info struct (NOVOSBOOT v1) | VERIFIED | boot/boot_info.h, mem_layout.h | M0 (keep) |
| Fixed VBox PIIX3 assumptions | PARTIAL | 8259 PIC, PIT, PS/2, PIO ATA; no probing fallback | M1/M5 |
| UEFI boot | ABSENT | — | M5 |
| ACPI (tables, power, SMP bring-up) | ABSENT | EBDA read only (vmm.c) | M5 |
| Device tree (ARM) | ABSENT | — | M5 |
| Interrupt-controller abstraction | ABSENT | PIC hard-coded (irq.c); LAPIC dormant (lapic.c) | M1 |
| Timer abstraction | ABSENT | PIT hard-coded (pit.c); TSC deferred (ADR-0078) | M1 |
| IOMMU / DMA isolation | ABSENT | W5 OPEN; BMIDE uses kernel pages | M2/M4 |

## Kernel execution

| Capability | Status | Evidence / note | Target |
| x86-32 protected-mode kernel, ring0/3 split | VERIFIED | GDT/IDT/TSS, USER-OK, mapping grid | M0 (keep) |
| x86-64 long-mode kernel | ABSENT | arch/x86_64 has headers only | proposed ADR |
| Strict-prio + RR scheduler, 10-tick slices | VERIFIED | SCHEDW/STARVE/SLEEP-OK | M0 (keep) |
| Real-time guarantees (bounded latency) | UNVERIFIED | no latency analysis exists | M2/M7 |
| Priority inheritance | ABSENT | documented (FS-CONCURRENCY §22) | M2 |
| SMP scheduling | ABSENT | APs parked (ADR-0069) | M2 |
| Lock ordering documented + audited | VERIFIED | LOCKING.md, rx→sock only nesting | M1 (keep) |
| IF-state discipline | VERIFIED | 38-site audit, L1/L2 fixed | M1 (keep) |
| Thread pool bound (16) + exhaustion proof | VERIFIED | THREX-OK burst=15 | M0 (keep) |

## Memory / isolation

| Capability | Status | Evidence / note | Target |
| PMM (bitmap + buddy backends) | VERIFIED | PMM-OK, shootout bench | M0 (keep) |
| VMM paging, kernel/user split (U/S) | VERIFIED | MEMMAP-OK grid | M0 (keep) |
| Read-only kernel text | VERIFIED | ADR-0067, runtime asserts | M1 (keep) |
| NX / W^X enforcement | ABSENT | no NX bit (W2) | M2 |
| ASLR | ABSENT | deterministic by design (ADR-0067) | M2 |
| KPTI / SMEP / SMAP use | ABSENT | threat-model mentions gating only | M2 |
| mmap/mprotect/munmap, COW, guard pages | ABSENT | validated: no impl (validate.c) | M2 |
| Centralized user-copy validation | VERIFIED | 38 sites, T8 vectors, ADR-0066 | M1 (keep) |
| Kernel heap exhaustion behavior | PARTIAL | NULL checks; no exhaustion battery (accepted) | M2 |
| Stack canaries / safe stacks | PARTIAL | thread-stack canary exists; scope unclear | M2 |

## Syscalls / IPC / userspace

| Capability | Status | Evidence / note | Target |
| Syscall ABI 0..37 (INT 0x80), errno model | VERIFIED | syscall.h, ABI.md, C/D/F batteries | M1 (freeze policy) |
| Validation-first + snapshot/copy discipline | VERIFIED | SYSCALL-ARGUMENTS.md, ADR-0066 | M1 (keep) |
| Process spawn/exec, caps, rlimits, creds, jails | VERIFIED | CAPS/RLIM/PERM/JAIL-OK | M1 (keep) |
| No process reaping | PARTIAL | accepted (DIAGNOSTICS §15) | M2 |
| No thread enumeration ABI | PARTIAL | accepted (query-by-tid only) | M2 |
| fork/clone/mmap/pipes/shm/signals/waitpid | ABSENT | grep-verified: no impl | M2/M8 |
| Libc (9 functions: io + string + strtoi) | PARTIAL | userspace/libs/libc (3+6 fns) | M8 |
| POSIX coverage | ABSENT | ~0% beyond read/write/open/close | M8 |
| SDK + cross toolchain | PARTIAL | sdk/ headers + user.ld, x86-32 only | M8 |

## Storage

| Capability | Status | Evidence / note | Target |
| VFS + big-lock contract | VERIFIED | FILESYSTEM-CONCURRENCY.md, FSSTRESS-OK | M1 (keep) |
| RAMFS (read/write, permissions, refs) | VERIFIED | FS-OK, 7-row manifests | M0 (keep) |
| FAT32 / EXT4 / NOVA-FS readers + fsck | PARTIAL | drivers + replay; graft writes EROFS-guarded | M4 |
| Persistent read/write production FS | ABSENT | no journaling, no crash-consistency model | M4 |
| User-file persistence on graft volumes (v0.8) | VERIFIED (bounded) | put/cat/rm/ls on /disk + /nova survive genuine poweroff+reboot (demo screenshots); PERSIST-OK lifecycle battery every boot; synchronous writes, no write-back cache; crash-consistency under mid-write power loss NOT proven; 64 KB/file; test-fixture VDIs | v0.8 (keep; M4 for crash-proofing) |
| Block-device abstraction | ABSENT | ATA paths hard-wired (ata.c) | M4 |
| AHCI / NVMe | ABSENT | — | M4 |
| Flash wear-leveling / FTL assumptions | ABSENT | — | M4 |
| Encryption at rest | ABSENT | — | M4/M6 |
| ATA PIO + BMIDE DMA, LBA28 ≤128GiB | PARTIAL | ata.c; polling only, no IRQ | M4 |

## Networking

| Capability | Status | Evidence / note | Target |
| Virtio-net + loopback, ARP/DHCP/DNS-client | VERIFIED | NET/VIRTIO/DHCP-OK, irq-counted | M0 (keep) |
| UDP sockets (8-pool, generations, waiter matrix) | VERIFIED | SOCK/SOCKBLK/NETSTRESS-OK, ADR-0080 | M3 (keep) |
| Bounds-first parsers + wire corpus | VERIFIED | 13 shapes + 15 seeds + 2000-frame flood | M3 (keep) |
| TCP | ABSENT | — | M3 |
| IPv6 | ABSENT | — | M3 |
| Fragmentation / reassembly | ABSENT | refused by design (documented) | M3 |
| Routing / firewall / filtering | ABSENT | — | M3 |
| Retransmission / congestion control | ABSENT | (needs TCP) | M3 |
| Real Ethernet controllers | ABSENT | virtio-transitional only | M4/M5 |
| TLS / cert validation / SSH / HTTP / MQTT / VPN | ABSENT | — | M3 |

## Crypto / trust

| Capability | Status | Evidence / note | Target |
| Any crypto primitive in-tree | ABSENT | grep-verified: none | M3 |
| CSPRNG + entropy policy | ABSENT | — | M3 |
| Key storage / rotation / revocation | ABSENT | — | M3/M6 |
| Signature verification | ABSENT | — | M3/M6 |
| Secure / measured boot, TPM, attestation | ABSENT | custom stage1, no verification | M6 |
| Signed updates / rollback | ABSENT | — | M6 |

## Drivers / IoT

| Capability | Status | Evidence / note | Target |
| Driver lifecycle model (bind/probe/remove) | ABSENT | init calls drivers directly (pm_main) | M1/M4 |
| Driver isolation / DMA trust boundary | ABSENT | ring-0 FULL trust (W3 OPEN) | M2/M4 |
| PCI discovery | VERIFIED | pci_scan, VBox VID:DID match | M4 (keep) |
| UART (16550 console) | VERIFIED | serial.c COM1 | M5 (keep) |
| PS/2 keyboard (Set-1, blocking park) | VERIFIED | KBDBLK-OK | scope-limited |
| USB / I2C / SPI / GPIO / ADC | ABSENT | — | M5 |
| Wi-Fi / Bluetooth / cellular | ABSENT | — | M5 |
| Watchdog timers | ABSENT | — | M5/M7 |
| Power states / wakeup | ABSENT | hlt-idle only | M5 |
| Board support (ARM dev board) | ABSENT | — | M5 |
| Sensors / embedded peripherals | ABSENT | — | M5 |

## Production operations

| Capability | Status | Evidence / note | Target |
| Gate-driven selftests (50 markers, fuzz corpus) | VERIFIED | GATES-OK, CORPUS-OK 7/7, ci/gate.py | M0/M7 (keep) |
| Serial + structured diagnostics contract | VERIFIED | DIAGNOSTICS.md (15 items) | M6 (extend) |
| Service supervision / init deps | ABSENT | single spawn_init, no deps | M8 |
| Config management | ABSENT | — | M8 |
| Metrics/log export, remote admin | ABSENT | serial-file only | M6 |
| Packaging, signing, provenance, SBOM | ABSENT | ARCHITECTURE.md §10 design-only | M6 |
| Reproducible builds | PARTIAL | pinned mingw + one driver; no checksums/SBOM | M0/M6 |
| OTA / A-B / rollback / recovery | ABSENT | — | M6 |
| Device identity / enrollment / attestation | ABSENT | — | M6 |
| Fleet management / backup / crash pipeline | ABSENT | — | M6/M7 |
| Containers / VM isolation | ABSENT | — | M8 |
| Vuln disclosure + patch lifecycle | ABSENT | THREAT-MODEL references missing SECURITY.md | M1/M7 |
| License/provenance audit (dist/) | PARTIAL | Apache-2.0 LICENSE (ADR-0064); no SBOM | M0 |

## Reading guide

- VERIFIED rows are the v0.5 asset: ~35 capabilities with
  reproducible in-guest or host proof. The roadmap keeps
  every one of them green while building outward.
- ABSENT rows are not accusations; most were explicitly
  out of v0.5 scope. They become the milestone backlog
  (Target column → ROADMAP.md §Milestones).
- UNVERIFIED rows are the audit debt: claims the current
  methodology cannot establish (latency, leak-freedom,
  heap exhaustion). Each needs a named proof before it
  can gate a milestone.
