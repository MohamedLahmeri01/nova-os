/* NOVA OS ATA driver (Phase 7c PIO, channels in 7e, BMIDE DMA in 8c).
 * LBA28 single-sector commands. PIO (0x20/0x30) is the baseline;
 * after the selftest proves DMA equivalence, all traffic routes via
 * bus-master DMA (READ/WRITE DMA 0xC8/0xCA) with PRD tables. Status
 * polling with a bounded spin throughout (no IRQ reliance); the
 * 400ns select-settle uses alt-status reads (side-effect free).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "mm/pmm.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "ata: no arch backend for this architecture"
#endif

#define ATA_PRIM_BASE 0x1F0u
#define ATA_PRIM_ALT 0x3F6u
#define ATA_SEC_BASE 0x170u
#define ATA_SEC_ALT 0x376u
#define ATA_P_SDATA 0x0A1u

#define ATA_SR_BSY 0x80u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_DF 0x20u
#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u

#define ATA_CMD_READ 0x20u
#define ATA_CMD_WRITE 0x30u
#define ATA_CMD_READ_DMA 0xC8u
#define ATA_CMD_WRITE_DMA 0xCAu

/* BMIDE (Phase 8c): offsets from the channel base (primary +0,
 * secondary +8). Status bits: active/error/interrupt. Direction bit
 * 3 SET = device-to-memory (READS); clear = writes. (Intel names it
 * from the controller's side; VBox+QEMU sources agree on this
 * polarity, and VBox asserts it in its async thread.) */
#define BM_CMD_START 0x01u
#define BM_CMD_FROMDEV 0x08u
#define BM_ST_ACTIVE 0x01u
#define BM_ST_ERR 0x02u
#define BM_ST_INT 0x04u
#define BM_ST_CLEAR 0x06u
/* Bounded spins: each poll is a VM exit under VirtualBox (~µs), so
 * 10M polls = tens of seconds of wall time. 2M absorbs dynamic-VDI
 * first-write allocation stalls (~seconds worst case) yet still fails
 * audibly on dead drives (a learned flake: fresh-VDI first boot). */
#define ATA_SPIN_MAX 2000000u

/* LBA28 covers 128GiB; our disks are MBs (reject the rest loudly). */
#define ATA_LBA_MAX 0x0FFFFFFFu

static uint32_t g_bm_base;
static uint32_t g_prd_phys;
static int g_use_dma;

static int dma_sector(uint32_t drive, uint32_t lba, uint8_t *buf,
                      int is_write);
static int ata_set_dma_mode(uint32_t drive);

static int wait_clear_bsy(uint16_t status) {
    uint32_t i = 0;
    while (i++ < ATA_SPIN_MAX) {
        if ((arch_inb(status) & ATA_SR_BSY) == 0) {
            return NOVA_ESUCCESS;
        }
    }
    return -NOVA_EIO;
}

static int wait_drq(uint16_t status) {
    uint32_t i = 0;
    for (;;) {
        uint8_t st = arch_inb(status);
        if ((st & ATA_SR_BSY) == 0) {
            if ((st & ATA_SR_ERR) != 0 || (st & ATA_SR_DF) != 0) {
                return -NOVA_EIO;
            }
            if ((st & ATA_SR_DRQ) != 0) {
                return NOVA_ESUCCESS;
            }
        }
        if (i++ >= ATA_SPIN_MAX) {
            return -NOVA_EIO;
        }
    }
}

/* Channel ports from the drive number (0/1 primary, 2/3 secondary).
 * Returns the slave bit; stores base/alt for the command. */
static uint32_t chan_ports(uint32_t drive, uint16_t *base, uint16_t *alt) {
    if (drive <= ATA_DRIVE_SLAVE) {
        *base = ATA_PRIM_BASE;
        *alt = ATA_PRIM_ALT;
        return drive;
    }
    *base = ATA_SEC_BASE;
    *alt = ATA_SEC_ALT;
    return drive - 2u;
}

void ata_init(void) {
    uint8_t m = arch_inb(ATA_P_SDATA);
    const struct pci_dev *ide;
    /* IRQ14 = slave bit 6, IRQ15 = slave bit 7. Keep BIOS timer/
     * keyboard/cascade bits untouched (read-modify-write). Polling
     * never waits on these IRQs, so masking is race-free. */
    arch_outb(ATA_P_SDATA, (uint8_t)(m | 0xC0u));
    /* BMIDE setup (Phase 8c): find the IDE function, take BAR4
     * (I/O ports), enable bus mastering, allocate one PRD page
     * (identity-mapped: phys == virt). Absent/broken -> PIO stays.
     * NOTE: pci_scan here (pm_main orders ATA before the PCI gate;
     * the scan is pure port I/O with no dependencies). */
    pci_scan();
    ide = pci_find(0x8086u, 0x7010u);
    if (ide == 0) {
        ide = pci_find(0x8086u, 0x7111u);
    }
    if (ide != 0 && (ide->bar[4] & 1u) != 0) {
        uint16_t bm = (uint16_t)(ide->bar[4] & ~3u);
        uint32_t cmd;
        uint32_t cls;
        if (bm != 0) {
            cmd = pci_read_cfg(ide->bus, ide->slot, ide->func, 4);
            pci_write(ide->bus, ide->slot, ide->func, 4,
                      cmd | 0x04u);
            /* Cache-line + latency for bus mastering (BIOS leaves
             * both zero; bursting without them is undefined). */
            cls = pci_read_cfg(ide->bus, ide->slot, ide->func, 0x0Cu);
            cls &= ~0xFFFFu;
            cls |= 0x2008u; /* latency 0x20, line 0x08 (32B) */
            pci_write(ide->bus, ide->slot, ide->func, 0x0Cu, cls);
            g_bm_base = bm;
            g_prd_phys = pmm_alloc_contig(1);
            if (g_prd_phys == 0) {
                g_bm_base = 0;
            } else {
                uint32_t *prd0 = (uint32_t *)g_prd_phys;
                /* Zero the PRD page: entries past EOT must be
                 * deterministic (a controller that over-reads one
                 * entry meets zeros, not stale RAM). */
                for (uint32_t i = 0; i < 1024u; i++) {
                    prd0[i] = 0;
                }
                int ok = 1;
                /* DMA transfer mode per drive (drives reject DMA
                 * commands in PIO default mode). Any failure ->
                 * PIO for everything (fail safe, not sorry). */
                for (uint32_t d = 0; d < 4u; d++) {
                    if (ata_set_dma_mode(d) != 0) {
                        ok = 0;
                        break;
                    }
                }
                if (!ok) {
                    g_bm_base = 0;
                    serial_puts("ATA-NODMA\n");
                } else {
                    serial_puts("ATA-BMIDE base=");
                    serial_puthex32(g_bm_base);
                    serial_putc('\n');
                }
            }
        }
    }
}

int ata_read_sector(uint32_t drive, uint32_t lba, uint8_t *dst) {
    uint16_t base, alt, status;
    uint32_t slave;
    int rc;
    if (dst == 0) {
        return -NOVA_EINVAL;
    }
    if (drive > ATA_DRIVE_SEC_SLAVE || lba > ATA_LBA_MAX) {
        return -NOVA_EINVAL;
    }
    slave = chan_ports(drive, &base, &alt);
    status = (uint16_t)(base + 7u);
    if (g_use_dma && g_bm_base != 0) {
        return dma_sector(drive, lba, dst, 0);
    }
    /* IF save/restore (Phase H deadlock fix): blind cli/sti here
     * re-enabled interrupts inside a caller's irqsave critical
     * section (FS big lock), letting a same-priority waiter spin
     * with IF=0 while its holder starves — permanent. The PIO
     * sequence itself needs no IRQ delivery (pure polling). */
    {
        uint32_t ata_was = arch_irq_save();
        rc = wait_clear_bsy(status);
        if (rc == 0) {
            /* Drive/head: 0xE0 LBA-mode base | slave bit | LBA top 4. */
            arch_outb((uint16_t)(base + 6u),
                      (uint8_t)(0xE0u | (slave << 4) |
                                ((lba >> 24) & 0x0Fu)));
            /* 400ns settle (4 alt-status reads, no side effects). */
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_outb((uint16_t)(base + 2u), 1);
            arch_outb((uint16_t)(base + 3u), (uint8_t)(lba & 0xFFu));
            arch_outb((uint16_t)(base + 4u),
                      (uint8_t)((lba >> 8) & 0xFFu));
            arch_outb((uint16_t)(base + 5u),
                      (uint8_t)((lba >> 16) & 0xFFu));
            arch_outb((uint16_t)(base + 7u), ATA_CMD_READ);
            rc = wait_drq((uint16_t)(base + 7u));
            if (rc == 0) {
                for (uint32_t i = 0; i < 256u; i++) {
                    uint16_t w = arch_inw(base);
                    dst[2u * i] = (uint8_t)(w & 0xFFu);
                    dst[2u * i + 1u] = (uint8_t)(w >> 8);
                }
            }
        }
        arch_irq_restore(ata_was);
    }
    return rc;
}

int ata_write_sector(uint32_t drive, uint32_t lba, const uint8_t *src) {
    uint16_t base, alt, status;
    uint32_t slave;
    int rc;
    if (src == 0) {
        return -NOVA_EINVAL;
    }
    if (drive > ATA_DRIVE_SEC_SLAVE || lba > ATA_LBA_MAX) {
        return -NOVA_EINVAL;
    }
    slave = chan_ports(drive, &base, &alt);
    status = (uint16_t)(base + 7u);
    if (g_use_dma && g_bm_base != 0) {
        return dma_sector(drive, lba, (uint8_t *)src, 1);
    }
    /* Same IF save/restore as the read path (see above). */
    {
        uint32_t ata_was = arch_irq_save();
        rc = wait_clear_bsy(status);
        if (rc == 0) {
            arch_outb((uint16_t)(base + 6u),
                      (uint8_t)(0xE0u | (slave << 4) |
                                ((lba >> 24) & 0x0Fu)));
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_outb((uint16_t)(base + 2u), 1);
            arch_outb((uint16_t)(base + 3u), (uint8_t)(lba & 0xFFu));
            arch_outb((uint16_t)(base + 4u),
                      (uint8_t)((lba >> 8) & 0xFFu));
            arch_outb((uint16_t)(base + 5u),
                      (uint8_t)((lba >> 16) & 0xFFu));
            arch_outb((uint16_t)(base + 7u), ATA_CMD_WRITE);
            rc = wait_drq(status);
            if (rc == 0) {
                for (uint32_t i = 0; i < 256u; i++) {
                    uint16_t w = (uint16_t)src[2u * i] |
                                 ((uint16_t)src[2u * i + 1u] << 8);
                    arch_outw(base, w);
                }
                /* WRITE SECTORS signals completion via a second
                 * DRQ->idle cycle; poll BSY clear (bounded) to
                 * confirm the write. */
                rc = wait_clear_bsy(status);
            }
        }
        arch_irq_restore(ata_was);
    }
    return rc;
}

/* Select a DMA transfer mode (SET FEATURES 0x03). Drives power up
 * in PIO mode, where READ/WRITE DMA commands abort. UDMA2 (33MB/s,
 * PIIX4 ceiling); failure disables DMA (PIO carries on). */
static int ata_set_dma_mode(uint32_t drive) {
    uint16_t base, alt, status;
    uint32_t slave;
    int rc = -NOVA_EIO;
    slave = chan_ports(drive, &base, &alt);
    status = (uint16_t)(base + 7u);
    /* Same IF save/restore as the read path (see above). */
    {
        uint32_t ata_was = arch_irq_save();
        if (wait_clear_bsy(status) == 0) {
            arch_outb((uint16_t)(base + 6u),
                      (uint8_t)(0xE0u | (slave << 4)));
            arch_inb(alt);
            arch_inb(alt);
            arch_outb((uint16_t)(base + 1u), 0x03u);
            /* Multiword DMA 2 (conservative: needs no UDMA
             * controller registers; PIIX-era baseline). */
            arch_outb((uint16_t)(base + 2u), 0x22u);
            arch_outb((uint16_t)(base + 7u), 0xEFu);
            /* No data phase: BSY clear + no ERR soon. */
            {
                uint32_t i = 0;
                for (;;) {
                    uint8_t st = arch_inb(status);
                    if ((st & ATA_SR_BSY) == 0) {
                        rc = ((st & (ATA_SR_ERR | ATA_SR_DF)) != 0)
                                 ? -NOVA_EIO
                                 : NOVA_ESUCCESS;
                        break;
                    }
                    if (i++ >= ATA_SPIN_MAX) {
                        break;
                    }
                }
            }
        }
        arch_irq_restore(ata_was);
    }
    return rc;
}

/* Build PRD entries for one 512B transfer (Phase 8c). Splits the
 * caller buffer at 4KB-page and 64KB boundaries (identity-mapped:
 * phys == virt); at most 3 entries for our sizes. EOT on the last.
 * Returns the entry count, or 0 when nothing fits (never: callers
 * pass <=512B, which always fits in 3). */
static uint32_t prd_build(uint32_t *prd, const uint8_t *buf) {
    uint32_t addr = (uint32_t)buf;
    uint32_t left = 512u;
    uint32_t n = 0;
    while (left > 0 && n < 3u) {
        uint32_t page_end = (addr + 4096u) & ~4095u;
        uint32_t seg_end = (addr + 65536u) & ~65535u;
        uint32_t chunk = left;
        if (page_end - addr < chunk) {
            chunk = page_end - addr;
        }
        if (seg_end - addr < chunk) {
            chunk = seg_end - addr;
        }
        prd[n * 2u] = addr;
        prd[n * 2u + 1u] = chunk;
        addr += chunk;
        left -= chunk;
        n++;
    }
    if (left != 0) {
        return 0;
    }
    prd[n * 2u - 1u] |= 0x80000000u; /* EOT */
    return n;
}

/* One DMA sector (caller validated drive/lba/buf). Direction: reads
 * are device->memory (dir bit clear), writes the reverse. */
static int dma_sector(uint32_t drive, uint32_t lba, uint8_t *buf,
                      int is_write) {
    uint16_t base, alt, status;
    uint16_t bm;
    uint32_t slave;
    uint32_t *prd;
    int rc;
    slave = chan_ports(drive, &base, &alt);
    status = (uint16_t)(base + 7u);
    bm = (uint16_t)(g_bm_base + ((drive > ATA_DRIVE_SLAVE) ? 8u : 0u));
    prd = (uint32_t *)g_prd_phys;
    if (prd_build(prd, buf) == 0) {
        return -NOVA_EINVAL;
    }
    /* Same IF save/restore as the read path (see above). */
    {
        uint32_t ata_was = arch_irq_save();
        rc = wait_clear_bsy(status);
        if (rc == 0) {
            uint8_t dir = (uint8_t)(is_write ? 0x00u : BM_CMD_FROMDEV);
            /* Engine setup first (stopped, direction, cleared
             * status), then the ATA command, then START
             * (Intel order). */
            arch_outb(bm, 0x00u);
            arch_outl((uint16_t)(bm + 4u), g_prd_phys);
            arch_outb(bm, dir);
            arch_outb((uint16_t)(bm + 2u), BM_ST_CLEAR);
            arch_outb((uint16_t)(base + 6u),
                      (uint8_t)(0xE0u | (slave << 4) |
                                ((lba >> 24) & 0x0Fu)));
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_inb(alt);
            arch_outb((uint16_t)(base + 2u), 1);
            arch_outb((uint16_t)(base + 3u), (uint8_t)(lba & 0xFFu));
            arch_outb((uint16_t)(base + 4u),
                      (uint8_t)((lba >> 8) & 0xFFu));
            arch_outb((uint16_t)(base + 5u),
                      (uint8_t)((lba >> 16) & 0xFFu));
            arch_outb((uint16_t)(base + 7u),
                      (uint8_t)(is_write ? ATA_CMD_WRITE_DMA
                                         : ATA_CMD_READ_DMA));
            arch_outb(bm, (uint8_t)(dir | BM_CMD_START));
            /* Completion: Interrupt bit (bounded); error bit
             * fails. */
            {
                uint32_t i = 0;
                for (;;) {
                    uint8_t st = arch_inb((uint16_t)(bm + 2u));
                    if ((st & BM_ST_INT) != 0) {
                        rc = ((st & BM_ST_ERR) != 0) ? -NOVA_EIO
                                                     : NOVA_ESUCCESS;
                        break;
                    }
                    if (i++ >= ATA_SPIN_MAX) {
                        rc = -NOVA_EIO;
                        break;
                    }
                }
            }
            arch_outb(bm, dir);
            arch_outb((uint16_t)(bm + 2u), BM_ST_CLEAR);
        }
        arch_irq_restore(ata_was);
    }
    return rc;
}

int ata_selftest(void) {
    /* 512B stack is fine; 1KB+ frames trip __chkstk_ms (no libgcc). */
    static uint8_t sec[512];
    uint32_t i;
    int found = 0;
    if (ata_read_sector(ATA_DRIVE_MASTER, 0, sec) != 0) {
        serial_puts("ATA-FAIL read\n");
        return -1;
    }
    if (sec[510] != 0x55u || sec[511] != 0xAAu) {
        serial_puts("ATA-FAIL sig\n");
        return -1;
    }
    /* Our own stage1 carries the NVSC tag (nova.py patches the sector
     * count right after it): proves we read OUR disk, not garbage. */
    for (i = 0; i + 4u <= 512u; i++) {
        if (sec[i] == 'N' && sec[i + 1u] == 'V' && sec[i + 2u] == 'S' &&
            sec[i + 3u] == 'C') {
            found = 1;
            break;
        }
    }
    if (!found) {
        serial_puts("ATA-FAIL tag\n");
        return -1;
    }
    /* Write path (slave scratch LBA 100: past the FAT volume's 64
     * sectors, inside the 1MB image; pattern, verify, restore zeros
     * so the fixture stays pristine for the FAT tests). */
    {
        uint32_t i;
        for (i = 0; i < 512u; i++) {
            sec[i] = (uint8_t)(i & 0xFFu);
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, 100, sec) != 0) {
            serial_puts("ATA-FAIL write\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            sec[i] = 0;
        }
        if (ata_read_sector(ATA_DRIVE_SLAVE, 100, sec) != 0) {
            serial_puts("ATA-FAIL reread\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            if (sec[i] != (uint8_t)(i & 0xFFu)) {
                serial_puts("ATA-FAIL data\n");
                return -1;
            }
        }
        for (i = 0; i < 512u; i++) {
            sec[i] = 0;
        }
        if (ata_write_sector(ATA_DRIVE_SLAVE, 100, sec) != 0) {
            serial_puts("ATA-FAIL restore\n");
            return -1;
        }
    }
    /* DMA equivalence (Phase 8c): same bytes via DMA as PIO, on both
     * paths; then route everything via DMA (all later FS gates prove
     * it under load). Absent BMIDE keeps PIO (other hardware). */
    if (g_bm_base != 0 && g_prd_phys != 0) {
        static uint8_t dsec[512];
        uint32_t i;
        if (ata_read_sector(ATA_DRIVE_MASTER, 0, sec) != 0) {
            serial_puts("ATA-FAIL dma-base\n");
            return -1;
        }
        if (dma_sector(ATA_DRIVE_MASTER, 0, dsec, 0) != 0) {
            serial_puts("ATA-FAIL dma-read\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            if (dsec[i] != sec[i]) {
                serial_puts("ATA-FAIL dma-data\n");
                return -1;
            }
        }
        for (i = 0; i < 512u; i++) {
            dsec[i] = (uint8_t)(0xA5u ^ (i & 0xFFu));
        }
        if (dma_sector(ATA_DRIVE_SLAVE, 100, dsec, 1) != 0) {
            serial_puts("ATA-FAIL dma-write\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            dsec[i] = 0;
        }
        if (dma_sector(ATA_DRIVE_SLAVE, 100, dsec, 0) != 0) {
            serial_puts("ATA-FAIL dma-reread\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            if (dsec[i] != (uint8_t)(0xA5u ^ (i & 0xFFu))) {
                serial_puts("ATA-FAIL dma-bytes\n");
                return -1;
            }
        }
        for (i = 0; i < 512u; i++) {
            dsec[i] = 0;
        }
        if (dma_sector(ATA_DRIVE_SLAVE, 100, dsec, 1) != 0) {
            serial_puts("ATA-FAIL dma-restore\n");
            return -1;
        }
        g_use_dma = 1;
        serial_puts("DMA-OK\n");
    } else {
        serial_puts("DMA-SKIP\n");
    }
    serial_puts("ATA-OK\n");
    return NOVA_ESUCCESS;
}
