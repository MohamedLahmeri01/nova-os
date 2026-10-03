/* NOVA OS ATA PIO driver (Phase 7c).
 * Primary channel (0x1F0), LBA28 READ SECTORS (0x20), one sector per
 * command (multi-sector PIO complicates the DRQ loop for no gain at
 * our sizes). Status polling with a bounded spin: BSY clear, then DRQ
 * set; ERR/DF fail fast. The 400ns select-settle uses alt-status
 * reads (side-effect free, unlike the command port).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "drivers/ata.h"
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

#define ATA_P_DATA 0x1F0u
#define ATA_P_ERR 0x1F1u
#define ATA_P_COUNT 0x1F2u
#define ATA_P_LBA0 0x1F3u
#define ATA_P_LBA1 0x1F4u
#define ATA_P_LBA2 0x1F5u
#define ATA_P_DRIVE 0x1F6u
#define ATA_P_CMD 0x1F7u
#define ATA_P_STATUS 0x1F7u
#define ATA_P_ALT 0x3F6u
#define ATA_P_SDATA 0x0A1u

#define ATA_SR_BSY 0x80u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_DF 0x20u
#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u

#define ATA_CMD_READ 0x20u
#define ATA_CMD_WRITE 0x30u
/* Bounded spins: each poll is a VM exit under VirtualBox (~µs), so
 * 10M polls = tens of seconds of wall time. 2M absorbs dynamic-VDI
 * first-write allocation stalls (~seconds worst case) yet still fails
 * audibly on dead drives (a learned flake: fresh-VDI first boot). */
#define ATA_SPIN_MAX 2000000u

/* LBA28 covers 128GiB; our disks are MBs (reject the rest loudly). */
#define ATA_LBA_MAX 0x0FFFFFFFu

static int wait_clear_bsy(void) {
    uint32_t i = 0;
    while (i++ < ATA_SPIN_MAX) {
        if ((arch_inb(ATA_P_STATUS) & ATA_SR_BSY) == 0) {
            return NOVA_ESUCCESS;
        }
    }
    return -NOVA_EIO;
}

static int wait_drq(void) {
    uint32_t i = 0;
    for (;;) {
        uint8_t st = arch_inb(ATA_P_STATUS);
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

void ata_init(void) {
    uint8_t m = arch_inb(ATA_P_SDATA);
    /* IRQ14 = slave bit 6, IRQ15 = slave bit 7. Keep BIOS timer/
     * keyboard/cascade bits untouched (read-modify-write). Polling
     * never waits on these IRQs, so masking is race-free. */
    arch_outb(ATA_P_SDATA, (uint8_t)(m | 0xC0u));
}

int ata_read_sector(uint32_t drive, uint32_t lba, uint8_t *dst) {
    int rc;
    if (dst == 0) {
        return -NOVA_EINVAL;
    }
    if (drive > ATA_DRIVE_SLAVE || lba > ATA_LBA_MAX) {
        return -NOVA_EINVAL;
    }
    __asm__ volatile("cli" ::: "memory");
    rc = wait_clear_bsy();
    if (rc == 0) {
        /* Drive/head: 0xE0 LBA-mode base | slave bit | LBA top 4. */
        arch_outb(ATA_P_DRIVE,
                  (uint8_t)(0xE0u | (drive << 4) |
                            ((lba >> 24) & 0x0Fu)));
        /* 400ns settle (4 alt-status reads, no side effects). */
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_outb(ATA_P_COUNT, 1);
        arch_outb(ATA_P_LBA0, (uint8_t)(lba & 0xFFu));
        arch_outb(ATA_P_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
        arch_outb(ATA_P_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
        arch_outb(ATA_P_CMD, ATA_CMD_READ);
        rc = wait_drq();
        if (rc == 0) {
            for (uint32_t i = 0; i < 256u; i++) {
                uint16_t w = arch_inw(ATA_P_DATA);
                dst[2u * i] = (uint8_t)(w & 0xFFu);
                dst[2u * i + 1u] = (uint8_t)(w >> 8);
            }
        }
    }
    __asm__ volatile("sti" ::: "memory");
    return rc;
}

int ata_write_sector(uint32_t drive, uint32_t lba, const uint8_t *src) {
    int rc;
    if (src == 0) {
        return -NOVA_EINVAL;
    }
    if (drive > ATA_DRIVE_SLAVE || lba > ATA_LBA_MAX) {
        return -NOVA_EINVAL;
    }
    __asm__ volatile("cli" ::: "memory");
    rc = wait_clear_bsy();
    if (rc == 0) {
        arch_outb(ATA_P_DRIVE,
                  (uint8_t)(0xE0u | (drive << 4) |
                            ((lba >> 24) & 0x0Fu)));
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_inb(ATA_P_ALT);
        arch_outb(ATA_P_COUNT, 1);
        arch_outb(ATA_P_LBA0, (uint8_t)(lba & 0xFFu));
        arch_outb(ATA_P_LBA1, (uint8_t)((lba >> 8) & 0xFFu));
        arch_outb(ATA_P_LBA2, (uint8_t)((lba >> 16) & 0xFFu));
        arch_outb(ATA_P_CMD, ATA_CMD_WRITE);
        rc = wait_drq();
        if (rc == 0) {
            for (uint32_t i = 0; i < 256u; i++) {
                uint16_t w = (uint16_t)src[2u * i] |
                             ((uint16_t)src[2u * i + 1u] << 8);
                arch_outw(ATA_P_DATA, w);
            }
            /* WRITE SECTORS signals completion via a second DRQ->idle
             * cycle; poll BSY clear (bounded) to confirm the write. */
            rc = wait_clear_bsy();
        }
    }
    __asm__ volatile("sti" ::: "memory");
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
    serial_puts("ATA-OK\n");
    return NOVA_ESUCCESS;
}
