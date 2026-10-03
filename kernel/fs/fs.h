/* NOVA OS filesystem interface (Phase 7: VFS + RAMFS).
 * Single mount (ramfs at /). Nodes are dirs or files; files hold a
 * kmalloc'd growable buffer (cap FS_MAX_FILE). Open files are
 * descriptions (node + offset + mode) referenced by per-process fds.
 * All functions return 0 or negative -errno (see syscall.h); paths
 * are absolute, FS_MAX_PATH capped, looked up under one static root.
 */
#ifndef NOVA_FS_H
#define NOVA_FS_H

#include <stdint.h>

#include "process/process.h"

#define FS_MAX_PATH 256u
#define FS_MAX_NAME 32u
#define FS_MAX_FILE (64u * 1024u)
#define FS_MAX_DEPTH 16u

/* Open flags (low 2 bits access mode, mirroring POSIX low bits). */
#define FS_O_RDONLY 0u
#define FS_O_WRONLY 1u
#define FS_O_RDWR 2u
#define FS_O_ACCMODE 3u
#define FS_O_CREAT 0x40u

struct fs_node {
    char name[FS_MAX_NAME];
    int is_dir;
    struct fs_node *parent;
    struct fs_node *child;   /* first child (dirs only) */
    struct fs_node *sibling; /* next under same parent */
    uint8_t *data;           /* files only */
    uint32_t size;
    uint32_t cap;
};

struct fs_file {
    struct fs_node *node;
    uint32_t off;
    int readable;
    int writable;
    /* Detached (FAT-materialized) nodes are owned by the description:
     * close frees node data + node, not just the descriptor. */
    int owns_node;
    /* FAT source path for write-through (valid iff owns_node). */
    char fat_path[FS_MAX_PATH + 1u];
};

/* /disk graft (Phase 7c): exact "disk" first component routes to the
 * FAT driver. "/disk" itself, "/disk/" prefix match; everything else
 * is ramfs. Returns nonzero when FAT owns the path. */
int fs_is_disk_path(const char *kpath);
/* /ext graft (Phase 7e): same rule for the EXT4 driver. */
int fs_is_ext_path(const char *kpath);
/* /nova graft (Phase 7f-1): same rule for the native driver. */
int fs_is_nova_path(const char *kpath);
/* Strip "/disk" -> FAT-relative path ("" means the FAT root). */
const char *fs_disk_rel(const char *kpath);

/* Build the tree + seed content. Nonzero on OOM (fatal: panic). */
int fs_init(void);
/* Deterministic selftest (content, nesting, errors, fd exhaustion).
 * Prints FS-OK. Nonzero on failure (fatal: panic). */
int fs_selftest(void);

/* Node lookup: absolute path -> node (0 on success). */
int fs_lookup(const char *path, struct fs_node **out);
/* Create an empty regular file (parent must exist and be a dir). */
int fs_create(const char *path, struct fs_node **out);
/* Create an empty directory (same parent rules as fs_create). */
int fs_mkdir(const char *path);
/* Node I/O (bounds-checked, grows files on write up to FS_MAX_FILE). */
int fs_read_node(struct fs_node *n, uint32_t off, uint8_t *dst,
                 uint32_t len, uint32_t *n_out);
int fs_write_node(struct fs_node *n, uint32_t off, const uint8_t *src,
                  uint32_t len, uint32_t *n_out);
/* Child name by index (readdir): 0 + NUL-terminated name, or -ENOENT
 * when index is past the last child. ENOTDIR for non-dirs. */
int fs_readdir(struct fs_node *dir, uint32_t index, char *name_out);

/* Open-file descriptions + fd allocation in a process. */
struct fs_file *fs_file_alloc(struct fs_node *n, uint32_t flags);
void fs_file_free(struct fs_file *f);
int fs_fd_alloc(struct process *p, struct fs_file *f);
struct fs_file *fs_fd_get(struct process *p, uint32_t fd);
int fs_fd_drop(struct process *p, uint32_t fd);

#endif /* NOVA_FS_H */
