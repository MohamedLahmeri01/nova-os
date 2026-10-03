/* NOVA OS ATA driver interface (Phase 7c, PIO polling).
 * Primary channel, LBA28, single-sector reads from master (drive 0,
 * the boot disk) or slave (drive 1, the data disk). Polling only:
 * the disk IRQ stays masked, so no handler races. All functions
 * return 0 or negative -errno (see syscall.h); timeouts are bounded
 * spins, never hangs (a dead disk fails the selftest, loudly).
 */
#ifndef NOVA_ATA_H
#define NOVA_ATA_H

#include <stdint.h>

#define ATA_DRIVE_MASTER 0u
#define ATA_DRIVE_SLAVE 1u

/* Read one 512B sector into dst. -EIO on timeout/error. */
int ata_read_sector(uint32_t drive, uint32_t lba, uint8_t *dst);
/* Write one 512B sector from src (WRITE SECTORS 0x30, same polling).
 * -EIO on timeout/error. No safety interlock (callers pick scratch
 * areas: the selftest uses slave LBA 100, past the FAT volume). */
int ata_write_sector(uint32_t drive, uint32_t lba, const uint8_t *src);
/* Mask the ATA IRQs (14/15) on the slave PIC (read-modify-write).
 * The BIOS leaves them unmasked and our IDT is not loaded yet: the
 * completion IRQ would vector through garbage and triple-fault.
 * Must run before the first ATA command (irq_init remaps later). */
void ata_init(void);
/* Deterministic selftest: LBA0 of the boot disk must carry 0x55AA
 * and our NVSC tag. Prints ATA-OK. Nonzero on failure (fatal). */
int ata_selftest(void);

#endif /* NOVA_ATA_H */
