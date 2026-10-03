/* NOVA OS ATA driver interface (Phase 7c PIO, Phase 7e channels).
 * Primary (0x1F0) + secondary (0x170) channels, LBA28, single-sector
 * polling reads/writes. Drives 0/1 = primary master/slave (boot and
 * FAT data disks); 2/3 = secondary master/slave (EXT4 data disk).
 * Polling only: disk IRQs stay masked, no handler races. All
 * functions return 0 or negative -errno; timeouts are bounded spins.
 */
#ifndef NOVA_ATA_H
#define NOVA_ATA_H

#include <stdint.h>

#define ATA_DRIVE_MASTER 0u
#define ATA_DRIVE_SLAVE 1u
#define ATA_DRIVE_SEC_MASTER 2u
#define ATA_DRIVE_SEC_SLAVE 3u

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
