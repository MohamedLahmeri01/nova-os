/* NOVA OS FAT32 reader interface (Phase 7c, read-only).
 * Superfloppy layout (BPB at LBA0, see tools/mkfat.py): 512B sectors,
 * parsed BPB (not assumed), FAT chain walks, 8.3 uppercase names
 * (input lowercased on match), subdirectories, no LFN. Sector source
 * is a callback (guest: ATA slave; host test: image bytes), so all
 * parsing logic is host-testable. Caps: files <= FS_MAX_FILE, path
 * depth <= FS_MAX_DEPTH, single static scratch sector (single-core).
 */
#ifndef NOVA_FAT32_H
#define NOVA_FAT32_H

#include <stdint.h>

/* Sector reader: 0 on success, negative -errno. */
typedef int (*fat_read_sector_t)(uint32_t lba, uint8_t *dst);

/* Parse + validate the BPB (signature, FAT32 markers, geometry).
 * Nonzero (negative -errno) on any mismatch. */
int fat_init(fat_read_sector_t rs);
/* File size + kind without reading data. */
int fat_stat(const char *path, uint32_t *size_out, int *is_dir_out);
/* Whole file into dst (cap max bytes; -ENOSPC when larger). */
int fat_read_file(const char *path, uint8_t *dst, uint32_t max,
                  uint32_t *len_out);
/* Nth visible entry of a dir ("" or "/" = root; dot entries hidden).
 * Copies "NAME.EXT" + NUL, returns name length, or -ENOENT at end. */
int fat_list_dir(const char *path, uint32_t index, char *name_out);
/* Deterministic selftest on the known image content. Prints FAT-OK.
 * Nonzero on failure (fatal). */
int fat_selftest(void);

#endif /* NOVA_FAT32_H */
