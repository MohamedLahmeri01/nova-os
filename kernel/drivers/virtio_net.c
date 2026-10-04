/* NOVA OS virtio-net modern driver (Phase 9b).
 * Transitional ID (1AF4:1000) but MODERN-only interface (the legacy
 * I/O BAR reads zero features): PCI caps walk to COMMON / NOTIFY /
 * ISR / DEVICE areas in BAR2 (MMIO, mapped uncached via vmm), then
 * the standard 1.0 handshake (reset, ACK+DRIVER, features incl.
 * VERSION_1, per-queue setup, DRIVER_OK). Queues: 64 descriptors
 * (1024B) + avail in one frame, used ring in the next (4K-aligned:
 * whole frames). RX owns 8 posted frames; TX bounces through one
 * frame (caller buffers may straddle pages). Single-descriptor
 * transfers in v0.1. Memory order: x86 TSO + compiler barriers.
 *
 * MMIO lives above 1GB (never in user address spaces by design):
 * every register touch runs in a brief cli-guarded kernel-CR3
 * window, so CPL3 can never poke the device directly.
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "drivers/virtio_net.h"
#include "drivers/pci.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "virtio-net: no arch backend for this architecture"
#endif

#define VQ_SIZE 64u
#define VQ_RX 0u
#define VQ_TX 1u
#define N_RX_BUFS 8u
/* 12B header: the device consumes the first 12 chain bytes as the
 * TX header (mergeable-RXBUF shape), so negotiate MRG_RXBUF and
 * speak 12B headers in both directions (spec 5.1.6). */
#define VNET_HDR 12u
#define ETH_MAX 1514u

#define VFEAT_MAC 0x20u
#define VFEAT_MRG 0x8000u
#define VFEAT_VER1_HI 0x01u /* bit 32: VERSION_1 lives in high dword */

#define VST_ACK 0x01u
#define VST_DRIVER 0x02u
#define VST_OK 0x04u
#define VST_FEAT_OK 0x08u
#define VST_FAILED 0x80u

#define VDESC_F_WRITE 2u

#define VCAP_COMMON 1u
#define VCAP_NOTIFY 2u
#define VCAP_ISR 3u
#define VCAP_DEVICE 4u

struct vring {
    uint8_t *desc;  /* 16B each */
    uint8_t *avail; /* flags + idx + ring[] */
    uint8_t *used;  /* flags + idx + (id,len)[] */
    uint16_t size;
    uint16_t last_used;
};

static uint8_t *g_mmio;
static uint32_t g_common;
static uint32_t g_notify;
static uint32_t g_notify_mul;
static uint32_t g_isr;
static uint32_t g_devcfg;
static uint16_t g_notify_off[2];
static struct vring g_rxq;
static struct vring g_txq;
static uint8_t g_mac[6];
static uint8_t *g_rx_frames[N_RX_BUFS];
static uint8_t g_tx_frame[4096];
static int g_ready;

/* MMIO accessors (Phase 9b FIX: width-correct volatile accesses —
 * byte-wise reconstruction of DWORD registers fails on VBox (reads
 * stuck at 0). GCC emits movl/movw/movb for these; all offsets are
 * naturally aligned. */
static uint8_t mm_r8(uint32_t o) {
    return *(volatile uint8_t *)(g_mmio + o);
}

static uint16_t mm_r16(uint32_t o) {
    return *(volatile uint16_t *)(g_mmio + o);
}

static uint32_t mm_r32(uint32_t o) {
    return *(volatile uint32_t *)(g_mmio + o);
}

static void mm_w8(uint32_t o, uint8_t v) {
    *(volatile uint8_t *)(g_mmio + o) = v;
}

static void mm_w16(uint32_t o, uint16_t v) {
    *(volatile uint16_t *)(g_mmio + o) = v;
}

static void mm_w32(uint32_t o, uint32_t v) {
    *(volatile uint32_t *)(g_mmio + o) = v;
}

/* Kernel-CR3 window for MMIO (user PDs must never map the device).
 * Balanced cli/sti; safe from any context (init or syscall). */
static uint32_t mm_enter(void) {
    uint32_t cur;
    __asm__ volatile("cli" ::: "memory");
    cur = arch_read_cr3();
    arch_write_cr3(vmm_kernel_pd());
    return cur;
}

static void mm_exit(uint32_t cur) {
    arch_write_cr3(cur);
    __asm__ volatile("sti" ::: "memory");
}

static void desc_set(uint8_t *d, uint32_t addr, uint32_t len,
                     uint16_t flags, uint16_t next) {
    /* virtq_desc: addr u64 @0, len u32 @8, flags u16 @12,
     * next u16 @14 (32-bit guest: addr high = 0). */
    d[0] = (uint8_t)(addr & 0xFFu);
    d[1] = (uint8_t)((addr >> 8) & 0xFFu);
    d[2] = (uint8_t)((addr >> 16) & 0xFFu);
    d[3] = (uint8_t)((addr >> 24) & 0xFFu);
    d[4] = 0;
    d[5] = 0;
    d[6] = 0;
    d[7] = 0;
    d[8] = (uint8_t)(len & 0xFFu);
    d[9] = (uint8_t)((len >> 8) & 0xFFu);
    d[10] = (uint8_t)((len >> 16) & 0xFFu);
    d[11] = (uint8_t)((len >> 24) & 0xFFu);
    d[12] = (uint8_t)(flags & 0xFFu);
    d[13] = (uint8_t)(flags >> 8);
    d[14] = (uint8_t)(next & 0xFFu);
    d[15] = (uint8_t)(next >> 8);
}

static void notify_q(uint16_t q) {
    uint32_t k = mm_enter();
    mm_w16(g_notify + g_notify_off[q] * g_notify_mul, q);
    mm_exit(k);
}

static void rx_post(uint16_t d, uint8_t *frame) {
    uint16_t aidx;
    desc_set(g_rxq.desc + d * 16u, (uint32_t)frame,
             VNET_HDR + ETH_MAX, VDESC_F_WRITE, 0);
    aidx = (uint16_t)(g_rxq.avail[2] | ((uint16_t)g_rxq.avail[3] << 8));
    g_rxq.avail[4 + (aidx % g_rxq.size) * 2u] = (uint8_t)(d & 0xFFu);
    g_rxq.avail[4 + (aidx % g_rxq.size) * 2u + 1u] =
        (uint8_t)(d >> 8);
    __asm__ volatile("" ::: "memory");
    aidx++;
    g_rxq.avail[2] = (uint8_t)(aidx & 0xFFu);
    g_rxq.avail[3] = (uint8_t)(aidx >> 8);
    g_rx_frames[d] = frame;
    if (g_ready) {
        notify_q(VQ_RX);
    }
}

/* Walk PCI caps for the four virtio areas (all must be on BAR2;
 * refuse anything else: no MMIO mapping surprises). */
static int find_caps(const struct pci_dev *dev) {
    uint32_t cap;
    uint32_t guard = 0;
    uint32_t bar2;
    int have_c = 0, have_n = 0, have_i = 0, have_d = 0;
    if ((dev->bar[2] & 1u) != 0) {
        return -NOVA_EIO; /* need 32-bit MMIO BAR2 */
    }
    bar2 = dev->bar[2] & ~0xFu;
    if (bar2 == 0) {
        return -NOVA_EIO;
    }
    vmm_map_mmio(bar2);
    g_mmio = (uint8_t *)bar2;
    cap = pci_read_cfg(dev->bus, dev->slot, dev->func, 0x34u) & 0xFFu;
    while (cap != 0 && guard++ < 16u) {
        uint32_t h0 = pci_read_cfg(dev->bus, dev->slot, dev->func,
                                   cap);
        uint32_t h1 = pci_read_cfg(dev->bus, dev->slot, dev->func,
                                   cap + 4u);
        uint32_t h2 = pci_read_cfg(dev->bus, dev->slot, dev->func,
                                   cap + 8u);
        uint32_t type, bar, off;
        if ((h0 & 0xFFu) != 0x09u) {
            cap = (h0 >> 8) & 0xFFu;
            continue;
        }
        type = (h0 >> 24) & 0xFFu;
        bar = h1 & 0xFFu;
        off = h2;
        if (bar != 2u) {
            return -NOVA_EIO;
        }
        if (type == VCAP_COMMON) {
            g_common = off;
            have_c = 1;
        } else if (type == VCAP_NOTIFY) {
            g_notify = off;
            g_notify_mul = pci_read_cfg(dev->bus, dev->slot,
                                        dev->func, cap + 16u);
            have_n = 1;
        } else if (type == VCAP_ISR) {
            g_isr = off;
            have_i = 1;
        } else if (type == VCAP_DEVICE) {
            g_devcfg = off;
            have_d = 1;
        }
        cap = (h0 >> 8) & 0xFFu;
    }
    if (!have_c || !have_n || !have_i || !have_d) {
        return -NOVA_EIO;
    }
    /* Enable MMIO + bus mastering (BIOS leaves them off: MMIO reads
     * float without MEM, DMA never starts without MASTER). */
    {
        uint32_t cmd = pci_read_cfg(dev->bus, dev->slot, dev->func,
                                    4u);
        pci_write(dev->bus, dev->slot, dev->func, 4u, cmd | 0x06u);
    }
    return NOVA_ESUCCESS;
}

/* Init one queue via common cfg (size, 64-bit addrs, enable). */
static int queue_init(struct vring *q, uint8_t *fa, uint8_t *fb,
                      uint16_t idx) {
    uint32_t k = mm_enter();
    uint16_t n;
    mm_w16(g_common + 22u, idx);
    n = mm_r16(g_common + 24u);
    if (n == 0) {
        mm_exit(k);
        return -NOVA_EIO;
    }
    if (n > VQ_SIZE) {
        n = VQ_SIZE; /* driver picks; device allows fewer */
    }
    /* Report the size we will use: the queue reads as size 0 until
     * the driver writes QueueSize (spec 4.1.4.3) — without this the
     * device consumes nothing (used ring frozen, no ISR). */
    mm_w16(g_common + 24u, n);
    q->desc = fa;
    q->avail = fa + n * 16u;
    q->used = fb;
    q->size = n;
    q->last_used = 0;
    for (uint32_t i = 0; i < n * 16u; i++) {
        fa[i] = 0;
    }
    for (uint32_t i = 0; i < (uint32_t)n * 2u + 6u; i++) {
        q->avail[i] = 0;
    }
    for (uint32_t i = 0; i < (uint32_t)n * 8u + 6u; i++) {
        fb[i] = 0;
    }
    for (uint16_t i = 0; i + 1u < n; i++) {
        desc_set(fa + i * 16u, 0, 0, 0, (uint16_t)(i + 1u));
    }
    mm_w32(g_common + 32u, (uint32_t)fa);
    mm_w32(g_common + 36u, 0);
    mm_w32(g_common + 40u, (uint32_t)(fa + n * 16u));
    mm_w32(g_common + 44u, 0);
    mm_w32(g_common + 48u, (uint32_t)fb);
    mm_w32(g_common + 52u, 0);
    mm_w16(g_common + 28u, 1);
    g_notify_off[idx] = mm_r16(g_common + 30u);
    mm_exit(k);
    return NOVA_ESUCCESS;
}

int virtio_net_init(void) {
    const struct pci_dev *dev = pci_find(0x1AF4u, 0x1000u);
    uint32_t flo, fhi;
    uint32_t k;
    if (dev == 0) {
        return -NOVA_ENODEV;
    }
    if (find_caps(dev) != 0) {
        return -NOVA_EIO;
    }
    k = mm_enter();
    mm_w8(g_common + 20u, 0);
    mm_w8(g_common + 20u, VST_ACK | VST_DRIVER);
    serial_puts("VIRTIO-ST ");
    serial_puthex32(mm_r8(g_common + 20u));
    serial_putc('\n');
    mm_w32(g_common + 0u, 0);
    flo = mm_r32(g_common + 4u);
    mm_w32(g_common + 0u, 1);
    fhi = mm_r32(g_common + 4u);
    serial_puts("VIRTIO-F lo=");
    serial_puthex32(flo);
    serial_puts(" hi=");
    serial_puthex32(fhi);
    serial_putc('\n');
    /* MAC + MRG_RXBUF (12B headers, see VNET_HDR); nothing else. */
    mm_w32(g_common + 8u, 0);
    mm_w32(g_common + 12u, flo & (VFEAT_MAC | VFEAT_MRG));
    mm_w32(g_common + 8u, 1);
    mm_w32(g_common + 12u, fhi & VFEAT_VER1_HI);
    mm_w8(g_common + 20u, VST_ACK | VST_DRIVER | VST_FEAT_OK);
    if ((mm_r8(g_common + 20u) & VST_FEAT_OK) == 0) {
        mm_w8(g_common + 20u, VST_FAILED);
        mm_exit(k);
        return -NOVA_EIO;
    }
    if (mm_r16(g_common + 16u) < 2u) {
        mm_w8(g_common + 20u, VST_FAILED);
        mm_exit(k);
        return -NOVA_EIO; /* need RX + TX queues */
    }
    mm_exit(k);
    /* Queues: RX=0 (desc+avail, used), TX=1. Frames are PMM pages
     * (4K-aligned, <4GB, identity = phys). */
    {
        uint32_t qa = pmm_alloc_contig(1);
        uint32_t qb = pmm_alloc_contig(1);
        uint32_t qc = pmm_alloc_contig(1);
        uint32_t qd = pmm_alloc_contig(1);
        if (qa == 0 || qb == 0 || qc == 0 || qd == 0) {
            return -NOVA_ENOSPC;
        }
        if (queue_init(&g_rxq, (uint8_t *)qa, (uint8_t *)qb, VQ_RX) !=
            0) {
            return -NOVA_EIO;
        }
        if (queue_init(&g_txq, (uint8_t *)qc, (uint8_t *)qd, VQ_TX) !=
            0) {
            return -NOVA_EIO;
        }
        for (uint16_t i = 0; i < N_RX_BUFS && i < g_rxq.size; i++) {
            uint32_t f = pmm_alloc_contig(1);
            if (f == 0) {
                return -NOVA_ENOSPC;
            }
            rx_post(i, (uint8_t *)f);
        }
    }
    /* MAC from device config (6B at offset 0). */
    k = mm_enter();
    for (uint32_t i = 0; i < 6u; i++) {
        g_mac[i] = mm_r8(g_devcfg + i);
    }
    mm_exit(k);
    if ((g_mac[0] & 1u) != 0) {
        k = mm_enter();
        mm_w8(g_common + 20u, VST_FAILED);
        mm_exit(k);
        return -NOVA_EIO;
    }
    k = mm_enter();
    mm_w8(g_common + 20u,
          (uint8_t)(VST_ACK | VST_DRIVER | VST_FEAT_OK | VST_OK));
    serial_puts("VIRTIO-ST2 ");
    serial_puthex32(mm_r8(g_common + 20u));
    serial_putc('\n');
    mm_exit(k);
    g_ready = 1;
    notify_q(VQ_RX); /* let the device see posted RX buffers */
    serial_puts("VIRTIO-NOTIFY mul=");
    serial_puthex32(g_notify_mul);
    serial_puts(" off0=");
    serial_puthex32(g_notify_off[0]);
    serial_puts(" off1=");
    serial_puthex32(g_notify_off[1]);
    serial_putc('\n');
    serial_puts("VIRTIO-OK mac=");
    for (uint32_t i = 0; i < 6u; i++) {
        uint8_t b = g_mac[i];
        uint8_t hi = (uint8_t)(b >> 4);
        uint8_t lo = (uint8_t)(b & 0xFu);
        serial_putc((char)(hi < 10u ? '0' + hi : 'a' + hi - 10u));
        serial_putc((char)(lo < 10u ? '0' + lo : 'a' + lo - 10u));
        if (i < 5u) {
            serial_putc(':');
        }
    }
    serial_putc('\n');
    return NOVA_ESUCCESS;
}

const uint8_t *virtio_net_mac(void) {
    return g_mac;
}

int virtio_net_ready(void) {
    return g_ready;
}

int virtio_net_tx(const uint8_t *frame, uint32_t len) {
    uint16_t d;
    uint16_t aidx;
    uint32_t k;
    if (!g_ready || frame == 0 || len == 0 || len > ETH_MAX) {
        return -NOVA_EINVAL;
    }
    /* Bounce: header + frame contiguous in our TX page. */
    for (uint32_t i = 0; i < VNET_HDR; i++) {
        g_tx_frame[i] = 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        g_tx_frame[VNET_HDR + i] = frame[i];
    }
    d = 0;
    aidx = (uint16_t)(g_txq.avail[2] | ((uint16_t)g_txq.avail[3] << 8));
    /* Oldest-completed slot is reusable; refuse when every slot is
     * still in flight (unreachable while the device consumes). */
    if ((uint16_t)(aidx - g_txq.last_used) >= g_txq.size) {
        uint16_t uidx = (uint16_t)(g_txq.used[2] |
                                   ((uint16_t)g_txq.used[3] << 8));
        g_txq.last_used = uidx;
        if ((uint16_t)(aidx - g_txq.last_used) >= g_txq.size) {
            return -NOVA_EAGAIN;
        }
    }
    d = (uint16_t)(aidx % g_txq.size);
    desc_set(g_txq.desc + d * 16u, (uint32_t)g_tx_frame,
             VNET_HDR + len, 0, 0);
    g_txq.avail[4 + (aidx % g_txq.size) * 2u] = (uint8_t)(d & 0xFFu);
    g_txq.avail[4 + (aidx % g_txq.size) * 2u + 1u] =
        (uint8_t)(d >> 8);
    __asm__ volatile("" ::: "memory");
    aidx++;
    g_txq.avail[2] = (uint8_t)(aidx & 0xFFu);
    g_txq.avail[3] = (uint8_t)(aidx >> 8);
    k = mm_enter();
    mm_w16(g_notify + g_notify_off[VQ_TX] * g_notify_mul, VQ_TX);
    /* Ack ISR (clears the device condition; INTx stays handled). */
    (void)mm_r8(g_isr);
    mm_exit(k);
    return NOVA_ESUCCESS;
}

int virtio_net_rx(uint8_t *out, uint32_t max) {
    uint16_t used_idx;
    uint32_t n;
    uint32_t k;
    if (!g_ready || out == 0) {
        return -NOVA_EINVAL;
    }
    used_idx =
        (uint16_t)(g_rxq.used[2] | ((uint16_t)g_rxq.used[3] << 8));
    if (used_idx == g_rxq.last_used) {
        return -NOVA_EAGAIN;
    }
    n = 0;
    /* First completed packet (callers poll; re-post after copy). */
    {
        uint32_t slot = g_rxq.last_used % g_rxq.size;
        uint32_t id = (uint32_t)g_rxq.used[4 + slot * 8u] |
                      ((uint32_t)g_rxq.used[4 + slot * 8u + 1u] << 8);
        uint32_t len = (uint32_t)g_rxq.used[4 + slot * 8u + 4u] |
                       ((uint32_t)g_rxq.used[4 + slot * 8u + 5u] << 8) |
                       ((uint32_t)g_rxq.used[4 + slot * 8u + 6u]
                        << 16) |
                       ((uint32_t)g_rxq.used[4 + slot * 8u + 7u]
                        << 24);
        uint8_t *frame;
        if (id >= N_RX_BUFS || g_rx_frames[id] == 0) {
            g_rxq.last_used++;
            return -NOVA_EIO;
        }
        frame = g_rx_frames[id];
        /* Strip the 12B virtio-net header (MRG_RXBUF shape). */
        if (len < VNET_HDR || len - VNET_HDR > max) {
            rx_post((uint16_t)id, frame);
            g_rxq.last_used++;
            return -NOVA_ENOSPC;
        }
        for (n = 0; n < len - VNET_HDR; n++) {
            out[n] = frame[VNET_HDR + n];
        }
        rx_post((uint16_t)id, frame);
        g_rxq.last_used++;
    }
    k = mm_enter();
    (void)mm_r8(g_isr);
    mm_exit(k);
    return (int)n;
}

int virtio_net_selftest(void) {
    uint8_t probe[64];
    if (!g_ready) {
        serial_puts("VIRTIO-FAIL init\n");
        return -1;
    }
    if ((g_mac[0] & 1u) != 0) {
        serial_puts("VIRTIO-FAIL mac\n");
        return -1;
    }
    /* Oversize refuses (no heap spray via net). */
    for (uint32_t i = 0; i < sizeof(probe); i++) {
        probe[i] = (uint8_t)i;
    }
    if (virtio_net_tx(probe, ETH_MAX + 1u) != -NOVA_EINVAL) {
        serial_puts("VIRTIO-FAIL huge\n");
        return -1;
    }
    if (virtio_net_tx(0, 10) != -NOVA_EINVAL) {
        serial_puts("VIRTIO-FAIL null\n");
        return -1;
    }
    /* RX ring accounting: posted buffers == descriptors offered. */
    {
        uint16_t aidx = (uint16_t)(g_rxq.avail[2] |
                                   ((uint16_t)g_rxq.avail[3] << 8));
        uint32_t expect = (N_RX_BUFS < g_rxq.size) ? N_RX_BUFS
                                                  : g_rxq.size;
        if (aidx != expect) {
            serial_puts("VIRTIO-FAIL ring\n");
            return -1;
        }
    }
    return NOVA_ESUCCESS;
}
