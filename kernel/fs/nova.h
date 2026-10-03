/* NOVA OS native filesystem reader interface (Phase 7f-1, read-only).
 * Implements docs/NOVA-FS.md v0.1 over the mknova.py fixture: 4KB
 * blocks, 128B inodes, inline extents (no index support yet), no
 * journal replay (clean fixture; contents ignored), crc32c on
 * superblock + inodes (bitwise: correctness first, table later).
 * Sector source is a callback (guest: ATA secondary slave; host test:
 * image bytes). Caps mirror the other drivers.
 */
#ifndef NOVA_NOVAFS_H
#define NOVA_NOVAFS_H

#include <stdint.h>

/* Sector reader: 0 on success, negative -errno. */
typedef int (*nova_read_sector_t)(uint32_t lba, uint8_t *dst);

/* Parse + validate both superblock copies (magic, 4KB, checksums;
 * falls back to copy 1 when copy 0 fails). Nonzero (-errno) when
 * neither copy verifies. */
int nova_init(nova_read_sector_t rs);
/* File size + kind without reading data. */
int nova_stat(const char *path, uint32_t *size_out, int *is_dir_out);
/* Whole file into dst (cap max bytes; -ENOSPC when larger). */
int nova_read_file(const char *path, uint8_t *dst, uint32_t max,
                   uint32_t *len_out);
/* Nth entry of a dir ("" or "/" = root; dot entries hidden).
 * Copies the name + NUL, returns name length, or -ENOENT at end. */
int nova_list_dir(const char *path, uint32_t index, char *name_out);
/* Deterministic selftest on the known fixture content. Prints
 * NOVA-OK. Nonzero on failure (fatal). */
int nova_selftest(void);

#endif /* NOVA_NOVAFS_H */
