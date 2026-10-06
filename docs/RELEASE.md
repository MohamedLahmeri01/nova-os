# NOVA OS — Release checklist

For cutting `vX.Y.Z`. The suite enforces everything checkable;
the rest is human judgment.

## Mechanical

1. `VERSION` holds the release (`X.Y.Z`, single source).
2. `python tools/nova.py build image` clean (stage1 <= 510B,
   kernel <= 768 sectors).
3. `python tools/nova.py test` fully green (currently T1-T20:
   guests, hosts, ABI freshness, dist verification).
4. `python tools/nova.py dist` assembles
   `dist/nova-os-<ver>-<date>-<sha>/` (binaries, ABI.md,
   changelog, SDK readme, VERSION, sha256 manifest).
5. CHANGELOG gains a `## X.Y.Z - <date>` section (highlights +
   known limitations, honest).
6. Commit, tag `vX.Y.Z`, push both. The tag points at a green
   tree, never ahead of it.

## What "release" means here

0.x releases are checkpoints, not promises (ABI-STRATEGY: no
stability before 1.0). Each release must boot on the
constrained VM (2 CPU / 1024 MB, NAT + virtio), print every
gate marker, and serve the shell. Known limitations ship in
the notes, not in silence (threat model actors 5-7 and the
ADR deferral lists are the source).

## 0.1.0 scope (2026-10-05)

Phases 0-13d: boot, memory, scheduler/SMP, syscalls, shell,
filesystems (RAMFS/FAT32/EXT4/NOVA-FS + fsck), PCI/DMA/VGA,
networking (loopback/virtio/DHCP/UDP), security (caps,
rlimits, UIDs, jails), SDK + ABI + dist, desktop (fb,
compositor, text, windows, menu bar, dormant mouse), file
lifecycle, CPU quotas, native identity, on-disk chmod.
Not in 0.1.0: user sockets/DNS, widgets, mouse motion (needs
a talking 8042), IRQ-driven virtio, secure boot, IOMMU.

## 0.2.0 scope (2026-10-06)

Phases 14a-16-i on top of 0.1.0: user-space UDP sockets +
shell uloop (14a), DNS + nslookup (14b), virtio interrupts
fielded + counted with the PIC cascade fix (14c), widget
toolkit + about dialog (14d), click routing through
windows, widgets, and user space (14e-i..iii, SYS_WINCLICK
34), sleep/wakeup + blocking sockets + wire demux + single-
ingress RX + tick pump (15-i..v), blocking keyboard (16-i).
Suite 21/21 (was 20/20). ABI v1 + 1 call; GETKEY parks.
Not in 0.2.0: mouse motion (still dormant here), device-ISR
RX drain (reverted, see ADR-0061), secure boot, IOMMU, live
peer traffic for wire-to-socket (proven without a peer).
