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
/* Sector writer: same contract (guest: ATA secondary slave). */
typedef int (*nova_write_sector_t)(uint32_t lba, const uint8_t *src);

/* Parse + validate both superblock copies (magic, 4KB, checksums;
 * falls back to copy 1 when copy 0 fails). Replays committed
 * journal transactions (Phase 7f-2). Nonzero (-errno) when neither
 * copy verifies. */
int nova_init(nova_read_sector_t rs, nova_write_sector_t ws);
/* File size + kind without reading data. */
int nova_stat(const char *path, uint32_t *size_out, int *is_dir_out);
/* Identity readout (Phase 13c): uid/gid + perm bits (0777) from
 * the inode. 0 or -errno (EINVAL/ENOENT/EIO). */
int nova_inode_info(const char *path, uint32_t *uid_out,
                    uint32_t *gid_out, uint16_t *mode_out);
/* Whole file into dst (cap max bytes; -ENOSPC when larger). */
int nova_read_file(const char *path, uint8_t *dst, uint32_t max,
                   uint32_t *len_out);
/* Nth entry of a dir ("" or "/" = root; dot entries hidden).
 * Copies the name + NUL, returns name length, or -ENOENT at end. */
int nova_list_dir(const char *path, uint32_t index, char *name_out);
/* Write API (Phase 7f-2, journaled): create empty file (parents must
 * exist), write bytes at an offset (grows extents, cap FS_MAX_FILE,
 * no shrink), delete a file (frees blocks+inode, clears the entry),
 * create a directory (with . / ..). All return 0 or -errno. */
int nova_create_file(const char *path);
int nova_write_at(const char *path, uint32_t off, const uint8_t *src,
                  uint32_t len);
int nova_delete(const char *path);
int nova_mkdir(const char *path);
/* Change permission bits through the journal (type bits kept).
 * Owner-or-root enforced inside. 0 or -errno. */
int nova_chmod(const char *path, uint16_t mode);
/* Verify-only audit (Phase 7f-3): SB copies, bitmaps-vs-reality,
 * inode checksums, dir chains, orphans. Read-only (no repairs).
 * Returns the problem count (0 = clean). */
int nova_fsck(void);
/* Deterministic selftest on the known fixture content. Prints
 * NOVA-OK. Nonzero on failure (fatal). */
int nova_selftest(void);

#endif /* NOVA_NOVAFS_H */
