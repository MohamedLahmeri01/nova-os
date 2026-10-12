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
    /* Phase 10c ownership (POSIX-lite triplets): creator uid/gid,
     * 9 permission bits (is_dir stays separate, never in mode).
     * Foreign filesystems materialize opener-owned (vfat-style). */
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    /* Phase 13a lifetime: open descriptions hold refs (alloc/free
     * pair); unlink detaches the name and frees at refs==0 (open-
     * but-deleted files keep working, POSIX-style). Jail roots
     * hold a ref: rmdir of a jailed-in dir refuses EBUSY. */
    int unlinked;
    int refs;
};

struct fs_file {
    struct fs_node *node;
    uint32_t off;
    int readable;
    int writable;
    /* Detached (FAT-materialized) nodes are owned by the description:
     * close frees node data + node, not just the descriptor. */
    int owns_node;
    /* Write-through source path (FAT + NOVA-FS materialized files);
     * empty for read-only copies (EXT4). */
    char store_path[FS_MAX_PATH + 1u];
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

/* Node lookup: absolute path -> node (0 on success). Jailed
 * callers resolve under their jail root (".." clamps there,
 * grafts invisible: they live outside every jail). */
int fs_lookup(const char *path, struct fs_node **out);
/* Explicit-root walk (test seam + future mounts): root is "/"
 * and the ".." floor. Same rules as fs_lookup otherwise. */
int fs_lookup_at(struct fs_node *root, const char *path,
                 struct fs_node **out);
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

/* Permission bits for fs_can need-mask (owner/group/other share
 * the pattern; root bypasses everything). */
#define FS_MAY_READ 4u
#define FS_MAY_WRITE 2u
#define FS_MAY_EXEC 1u

/* Current credentials (calling thread's process; 0/0 with no
 * current thread: boot context is root). */
uint32_t fs_current_uid(void);
uint32_t fs_current_gid(void);
/* Current jail root (NULL = global namespace). Reads the calling
 * thread's process like the uid helpers. */
struct fs_node *fs_current_jail(void);

/* Access check: 0 or -EACCES (-EINVAL for NULL node or unknown
 * need bits). Pure over the node (host-testable). */
int fs_can(const struct fs_node *n, uint32_t uid, uint32_t gid,
           uint32_t need);

/* Open-time check for a resolved node: read needs R, write modes
 * need W (RDWR needs both). 0 or -errno (EINVAL/EACCES). Pure
 * over proc + node (guest unit-tested without dispatch). */
int fs_open_check(const struct process *p, const struct fs_node *n,
                  uint32_t flags);

/* Change permission bits (owner-or-root; dirs keep dir-ness, mode
 * holds perm bits only). Ramfs only: graft paths refuse EROFS
 * (on-disk permission editing is per-FS future work). */
int fs_chmod(const char *path, uint16_t mode);
/* Reference counting (nodes outlive their names while open). */
void fs_node_ref(struct fs_node *n);
void fs_node_unref(struct fs_node *n);
/* Delete a name (Phase 13a): files only (dirs refuse EISDIR);
 * parent-W enforced fail-closed like create; detached names free
 * at refs==0. Ramfs only here (graft deletes dispatch in the
 * syscall layer: fat_/nova_delete, ext refuses EROFS). */
int fs_unlink(const char *path);
/* Remove an empty directory (EBUSY for /, jailed-in, or held
 * nodes; ENOTEMPTY otherwise; ramfs only, like chmod). */
int fs_rmdir(const char *path);

/* Deterministic concurrency + stress proof (guest, Phase H):
 * same-priority pair through the locked syscalls (frames),
 * structural asserts + fsck. Prints FSSTRESS-OK. Nonzero on
 * failure (fatal: panic). */
int fs_concurrency_selftest(void);
/* Persistent graft lifecycle (guest, v0.8): create/write/
 * grow/reread/delete on the live FAT + NOVA-FS disks,
 * byte-exact, volume fsck-clean after. Prints PERSIST-OK.
 * Nonzero on failure (fatal: panic). */
int fs_persist_selftest(void);
/* Deterministic ownership proof (guest selftest): denials bind,
 * triplets behave, credential dispatch refuses rises. Prints
 * PERM-OK. Nonzero on failure (fatal: panic). */
int perm_selftest(void);
/* Deterministic confinement proof (guest selftest): jails hide
 * the globe, ".." clamps, nesting shrinks. Prints JAIL-OK.
 * Nonzero on failure (fatal: panic). */
int jail_selftest(void);

#endif /* NOVA_FS_H */
