/* NOVA OS EXT4 reader interface (Phase 7e, read-only).
 * Minimal subset over a single-block-group image (see tools/mkext4.py):
 * 4KB blocks, 128B inodes, no extents (classic direct + single
 * indirect), no journal, case-sensitive names, no symlinks/xattrs.
 * Sector source is a callback (guest: ATA secondary master; host
 * test: image bytes), so all parsing is host-testable. Caps: files
 * <= FS_MAX_FILE, path depth <= FS_MAX_DEPTH, static scratch block
 * (single-core, no reentrancy).
 */
#ifndef NOVA_EXT4_H
#define NOVA_EXT4_H

#include <stdint.h>

/* Sector reader: 0 on success, negative -errno. */
typedef int (*ext_read_sector_t)(uint32_t lba, uint8_t *dst);

/* Parse + validate superblock + group descriptor (magic, 4KB blocks,
 * single group, 128B inodes). Nonzero (negative -errno) on mismatch. */
int ext_init(ext_read_sector_t rs);
/* File size + kind without reading data. */
int ext_stat(const char *path, uint32_t *size_out, int *is_dir_out);
/* Whole file into dst (cap max bytes; -ENOSPC when larger). */
int ext_read_file(const char *path, uint8_t *dst, uint32_t max,
                  uint32_t *len_out);
/* Nth entry of a dir ("" or "/" = root; dot entries hidden).
 * Copies the name + NUL, returns name length, or -ENOENT at end. */
int ext_list_dir(const char *path, uint32_t index, char *name_out);
/* Deterministic selftest on the known image content. Prints EXT-OK.
 * Nonzero on failure (fatal). */
int ext_selftest(void);

#endif /* NOVA_EXT4_H */
