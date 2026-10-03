/* NOVA OS EXT4 reader (Phase 7e, read-only).
 * Straight-line parsing with revalidation at every step (a corrupt
 * disk returns errno, never loops: block walks are step-capped,
 * depths capped, rec_len progress enforced). Names match exactly
 * (ext4 is case-sensitive: no folding, unlike FAT).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "fs/ext4.h"
#include "fs/fs.h"
#include "syscall/syscall.h"

static ext_read_sector_t g_rs;
static uint32_t g_inotab;   /* inode table first block */
static uint32_t g_blocks;   /* total blocks (single group) */
static uint8_t g_blk[4096];

#define EXT_MAGIC 0xEF53u
#define EXT_BLOCKS 4096u
#define EXT_SECTORS_PER_BLOCK (EXT_BLOCKS / 512u)
#define EXT_INODE_SIZE 128u
#define EXT_INODES_PER_BLOCK (EXT_BLOCKS / EXT_INODE_SIZE)
#define EXT_ROOT_INO 2u
#define EXT_CHAIN_MAX 64u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read a whole 4KB block through the sector callback. */
static int read_block(uint32_t blk, uint8_t *dst) {
    uint32_t base = blk * EXT_SECTORS_PER_BLOCK;
    if (g_rs == 0 || dst == 0) {
        return -NOVA_EINVAL;
    }
    if (blk >= g_blocks && g_blocks != 0) {
        return -NOVA_EIO;
    }
    for (uint32_t s = 0; s < EXT_SECTORS_PER_BLOCK; s++) {
        if (g_rs(base + s, dst + s * 512u) != 0) {
            return -NOVA_EIO;
        }
    }
    return NOVA_ESUCCESS;
}

int ext_init(ext_read_sector_t rs) {
    uint32_t log_bs, blocks, inodes_grp, ino_size, rev;
    uint32_t bpg;
    if (rs == 0) {
        return -NOVA_EINVAL;
    }
    g_rs = rs;
    g_blocks = 0; /* unknown until SB parses: allow block 0 read */
    if (read_block(0, g_blk) != 0) {
        return -NOVA_EIO;
    }
    /* Superblock lives at byte 1024 (inside block 0). */
    {
        const uint8_t *sb = g_blk + 1024;
        if (rd16(sb + 56) != EXT_MAGIC) {
            return -NOVA_EIO;
        }
        log_bs = rd32(sb + 24);
        if (log_bs != 2) {
            return -NOVA_EIO; /* 4KB blocks only */
        }
        blocks = rd32(sb + 4);
        bpg = rd32(sb + 32);
        if (blocks == 0 || blocks > bpg) {
            return -NOVA_EIO; /* single group only */
        }
        inodes_grp = rd32(sb + 40);
        if (inodes_grp == 0) {
            return -NOVA_EIO;
        }
        rev = rd32(sb + 76);
        if (rev < 1) {
            return -NOVA_EIO;
        }
        ino_size = rd16(sb + 88);
        if (ino_size != EXT_INODE_SIZE) {
            return -NOVA_EIO;
        }
        if (rd32(sb + 92) != 0 || rd32(sb + 96) != 0 ||
            rd32(sb + 100) != 0) {
            return -NOVA_EIO; /* no journal/extents/extra features */
        }
    }
    g_blocks = blocks;
    /* Group descriptor: block 1, first 32B. */
    if (read_block(1, g_blk) != 0) {
        return -NOVA_EIO;
    }
    g_inotab = rd32(g_blk + 8);
    if (g_inotab == 0 || g_inotab >= blocks) {
        return -NOVA_EIO;
    }
    return NOVA_ESUCCESS;
}

/* Load inode `ino` into a 128B buffer (single-group numbering). */
static int inode_read(uint32_t ino, uint8_t *out) {
    uint32_t idx;
    uint32_t blk;
    uint32_t off;
    if (out == 0 || ino < 1) {
        return -NOVA_EINVAL;
    }
    idx = ino - 1u;
    blk = g_inotab + idx / EXT_INODES_PER_BLOCK;
    off = (idx % EXT_INODES_PER_BLOCK) * EXT_INODE_SIZE;
    if (read_block(blk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    for (uint32_t i = 0; i < EXT_INODE_SIZE; i++) {
        out[i] = g_blk[off + i];
    }
    return NOVA_ESUCCESS;
}

static int ino_is_dir(const uint8_t *ino) {
    return (rd16(ino) & 0xF000u) == 0x4000u;
}

static uint32_t ino_size(const uint8_t *ino) {
    return rd32(ino + 4);
}

/* Block number at file-block index i (direct + single indirect). */
static int ino_block(const uint8_t *ino, uint32_t i, uint32_t *out) {
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    if (i < 12u) {
        *out = rd32(ino + 40 + i * 4u);
        return NOVA_ESUCCESS;
    }
    if (i < 12u + 1024u) {
        uint32_t ind = rd32(ino + 40 + 12u * 4u);
        if (ind == 0) {
            *out = 0;
            return NOVA_ESUCCESS;
        }
        /* Indirect block holds 1024 u32s (spans 8 sectors). */
        if (read_block(ind, g_blk) != 0) {
            return -NOVA_EIO;
        }
        *out = rd32(g_blk + (i - 12u) * 4u);
        return NOVA_ESUCCESS;
    }
    return -NOVA_ENOSPC;
}

/* Path convention: absolute paths with an optional /ext prefix. */
static const char *strip_ext(const char *path) {
    if (path != 0 && path[0] == '/' && path[1] == 'e' &&
        path[2] == 'x' && path[3] == 't' &&
        (path[4] == 0 || path[4] == '/')) {
        if (path[4] == 0) {
            return "/";
        }
        return path + 4;
    }
    return path;
}

/* Resolve a dir path to its inode number ("" or "/" = root).
 * Dot/dotdot via an explicit inode stack (no recursion). */
static int dir_ino(const char *path, uint32_t *out) {
    uint32_t stack[FS_MAX_DEPTH + 1u];
    uint32_t depth = 0;
    const char *p;
    uint8_t ino[EXT_INODE_SIZE];
    if (path == 0 || out == 0) {
        return -NOVA_EINVAL;
    }
    stack[0] = EXT_ROOT_INO;
    p = path;
    while (*p == '/') {
        p++;
    }
    if (*p == 0) {
        *out = EXT_ROOT_INO;
        return NOVA_ESUCCESS;
    }
    for (;;) {
        const char *s = p;
        uint32_t len = 0;
        while (p[len] != 0 && p[len] != '/') {
            len++;
        }
        if (len == 0) {
            break;
        }
        if (len == 1 && s[0] == '.') {
            /* stay */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (depth > 0) {
                depth--;
            }
        } else {
            uint32_t found = 0;
            if (depth >= FS_MAX_DEPTH) {
                return -NOVA_ENAMETOOLONG;
            }
            if (inode_read(stack[depth], ino) != 0) {
                return -NOVA_EIO;
            }
            if (!ino_is_dir(ino)) {
                return -NOVA_ENOTDIR;
            }
            /* Scan the dir's direct blocks for the component. */
            {
                uint32_t size = ino_size(ino);
                uint32_t nblk = (size + EXT_BLOCKS - 1u) / EXT_BLOCKS;
                uint32_t b;
                if (nblk > 12u) {
                    nblk = 12u; /* dirs stay direct in our image */
                }
                for (b = 0; b < nblk && !found; b++) {
                    uint32_t dblk = 0;
                    uint32_t at = 0;
                    if (ino_block(ino, b, &dblk) != 0 || dblk == 0) {
                        return -NOVA_EIO;
                    }
                    if (read_block(dblk, g_blk) != 0) {
                        return -NOVA_EIO;
                    }
                    while (at + 8u <= EXT_BLOCKS) {
                        uint32_t ei = rd32(g_blk + at);
                        uint32_t rl = rd16(g_blk + at + 4u);
                        uint32_t nl = g_blk[at + 6u];
                        if (rl < 8u || at + rl > EXT_BLOCKS) {
                            return -NOVA_EIO;
                        }
                        if (ei != 0 && nl == len) {
                            uint32_t k = 0;
                            while (k < len &&
                                   g_blk[at + 8u + k] ==
                                       (uint8_t)s[k]) {
                                k++;
                            }
                            if (k == len) {
                                uint8_t ci[EXT_INODE_SIZE];
                                if (inode_read(ei, ci) != 0) {
                                    return -NOVA_EIO;
                                }
                                if (!ino_is_dir(ci)) {
                                    return -NOVA_ENOTDIR;
                                }
                                stack[++depth] = ei;
                                found = ei;
                                break;
                            }
                        }
                        if (rl == 0) {
                            return -NOVA_EIO;
                        }
                        at += rl;
                    }
                }
            }
            if (!found) {
                return -NOVA_ENOENT;
            }
        }
        p = s + len;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            break;
        }
    }
    *out = stack[depth];
    return NOVA_ESUCCESS;
}

/* Find an entry by exact name inside a dir inode: number + mode +
 * size. 0x00 inode marks end-of-scan (deleted/empty slots skipped). */
static int dir_find(uint32_t dino, const char *name, uint32_t len,
                    uint32_t *ino_out, uint8_t *type_out,
                    uint32_t *size_out) {
    uint8_t di[EXT_INODE_SIZE];
    uint32_t size;
    uint32_t nblk;
    if (name == 0 || len == 0 || len > 255u) {
        return -NOVA_EINVAL;
    }
    if (inode_read(dino, di) != 0) {
        return -NOVA_EIO;
    }
    if (!ino_is_dir(di)) {
        return -NOVA_ENOTDIR;
    }
    size = ino_size(di);
    nblk = (size + EXT_BLOCKS - 1u) / EXT_BLOCKS;
    if (nblk > 12u) {
        nblk = 12u;
    }
    for (uint32_t b = 0; b < nblk; b++) {
        uint32_t dblk = 0;
        uint32_t at = 0;
        if (ino_block(di, b, &dblk) != 0 || dblk == 0) {
            return -NOVA_EIO;
        }
        if (read_block(dblk, g_blk) != 0) {
            return -NOVA_EIO;
        }
        while (at + 8u <= EXT_BLOCKS) {
            uint32_t ei = rd32(g_blk + at);
            uint32_t rl = rd16(g_blk + at + 4u);
            uint32_t nl = g_blk[at + 6u];
            uint8_t ft = g_blk[at + 7u];
            if (rl < 8u || at + rl > EXT_BLOCKS || rl == 0) {
                return -NOVA_EIO;
            }
            if (ei != 0 && nl == len) {
                uint32_t k = 0;
                while (k < len &&
                       g_blk[at + 8u + k] == (uint8_t)name[k]) {
                    k++;
                }
                if (k == len) {
                    uint8_t ci[EXT_INODE_SIZE];
                    if (inode_read(ei, ci) != 0) {
                        return -NOVA_EIO;
                    }
                    if (ino_out != 0) {
                        *ino_out = ei;
                    }
                    if (type_out != 0) {
                        *type_out = ft;
                    }
                    if (size_out != 0) {
                        *size_out = ino_size(ci);
                    }
                    return NOVA_ESUCCESS;
                }
            }
            at += rl;
        }
    }
    return -NOVA_ENOENT;
}

/* Split "/a/b/C" into parent-dir inode + final name span. */
static int split_parent(const char *path, uint32_t *dino_out,
                        const char **last_out, uint32_t *len_out) {
    const char *last = 0;
    const char *p = path;
    char dir[FS_MAX_PATH + 1u];
    uint32_t dl;
    if (path == 0 || path[0] != '/' || dino_out == 0 ||
        last_out == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    p = path;
    while (*p == '/') {
        p++;
    }
    for (;;) {
        const char *s = p;
        uint32_t len = 0;
        while (p[len] != 0 && p[len] != '/') {
            len++;
        }
        if (len == 0) {
            break;
        }
        last = s;
        p = s + len;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            break;
        }
    }
    if (last == 0) {
        return -NOVA_EINVAL;
    }
    dl = (uint32_t)(last - path);
    {
        uint32_t i = 0;
        while (i < dl && i < FS_MAX_PATH) {
            dir[i] = path[i];
            i++;
        }
        dir[i] = 0;
        if (i == 0) {
            dir[0] = '/';
            dir[1] = 0;
        }
    }
    if (dir_ino(dir, dino_out) != 0) {
        /* Preserve ENOTDIR (through-a-file) vs ENOENT (missing). */
        return dir_ino(dir, dino_out);
    }
    {
        uint32_t len = 0;
        while (last[len] != 0 && last[len] != '/') {
            len++;
        }
        if (len == 0 || len > 255u) {
            return -NOVA_EINVAL;
        }
        *last_out = last;
        *len_out = len;
    }
    return NOVA_ESUCCESS;
}

int ext_stat(const char *path, uint32_t *size_out, int *is_dir_out) {
    uint32_t dino;
    const char *last;
    uint32_t len;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    int rc;
    path = strip_ext(path);
    if (path == 0) {
        return -NOVA_EINVAL;
    }
    {
        const char *p = path;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            if (size_out != 0) {
                *size_out = 0;
            }
            if (is_dir_out != 0) {
                *is_dir_out = 1;
            }
            return NOVA_ESUCCESS;
        }
        {
            uint32_t L = 0;
            while (p[L] != 0) {
                L++;
            }
            if (L > 0 && p[L - 1u] == '/') {
                uint32_t di = 0;
                if (dir_ino(path, &di) != 0) {
                    return -NOVA_ENOENT;
                }
                if (size_out != 0) {
                    *size_out = 0;
                }
                if (is_dir_out != 0) {
                    *is_dir_out = 1;
                }
                return NOVA_ESUCCESS;
            }
        }
    }
    rc = split_parent(path, &dino, &last, &len);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dino, last, len, &ei, &ft, &size);
    if (rc != 0) {
        uint32_t di = 0;
        if (dir_ino(path, &di) == 0) {
            if (size_out != 0) {
                *size_out = 0;
            }
            if (is_dir_out != 0) {
                *is_dir_out = 1;
            }
            return NOVA_ESUCCESS;
        }
        return rc;
    }
    if (size_out != 0) {
        *size_out = (ft == 2) ? 0 : size;
    }
    if (is_dir_out != 0) {
        *is_dir_out = (ft == 2);
    }
    return NOVA_ESUCCESS;
}

int ext_read_file(const char *path, uint8_t *dst, uint32_t max,
                  uint32_t *len_out) {
    uint32_t dino;
    const char *last;
    uint32_t len;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    uint8_t fi[EXT_INODE_SIZE];
    uint32_t got = 0;
    int rc;
    path = strip_ext(path);
    if (path == 0 || dst == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    rc = split_parent(path, &dino, &last, &len);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dino, last, len, &ei, &ft, &size);
    if (rc != 0) {
        return rc;
    }
    if (ft == 2) {
        return -NOVA_EISDIR;
    }
    if (size > max) {
        return -NOVA_ENOSPC;
    }
    if (inode_read(ei, fi) != 0) {
        return -NOVA_EIO;
    }
    while (got < size) {
        uint32_t bi = got / EXT_BLOCKS;
        uint32_t bo = got % EXT_BLOCKS;
        uint32_t n = EXT_BLOCKS - bo;
        uint32_t dblk = 0;
        if (n > size - got) {
            n = size - got;
        }
        if (ino_block(fi, bi, &dblk) != 0 || dblk == 0) {
            return -NOVA_EIO;
        }
        if (read_block(dblk, g_blk) != 0) {
            return -NOVA_EIO;
        }
        for (uint32_t i = 0; i < n; i++) {
            dst[got + i] = g_blk[bo + i];
        }
        got += n;
    }
    *len_out = got;
    return NOVA_ESUCCESS;
}

int ext_list_dir(const char *path, uint32_t index, char *name_out) {
    uint32_t dino;
    uint8_t di[EXT_INODE_SIZE];
    uint32_t size;
    uint32_t nblk;
    uint32_t seen = 0;
    int rc;
    path = strip_ext(path);
    if (path == 0 || name_out == 0) {
        return -NOVA_EINVAL;
    }
    rc = dir_ino(path, &dino);
    if (rc != 0) {
        return rc;
    }
    if (inode_read(dino, di) != 0) {
        return -NOVA_EIO;
    }
    if (!ino_is_dir(di)) {
        return -NOVA_ENOTDIR;
    }
    size = ino_size(di);
    nblk = (size + EXT_BLOCKS - 1u) / EXT_BLOCKS;
    if (nblk > 12u) {
        nblk = 12u;
    }
    for (uint32_t b = 0; b < nblk; b++) {
        uint32_t dblk = 0;
        uint32_t at = 0;
        if (ino_block(di, b, &dblk) != 0 || dblk == 0) {
            return -NOVA_EIO;
        }
        if (read_block(dblk, g_blk) != 0) {
            return -NOVA_EIO;
        }
        while (at + 8u <= EXT_BLOCKS) {
            uint32_t ei = rd32(g_blk + at);
            uint32_t rl = rd16(g_blk + at + 4u);
            uint32_t nl = g_blk[at + 6u];
            uint32_t k;
            if (rl < 8u || at + rl > EXT_BLOCKS || rl == 0) {
                return -NOVA_EIO;
            }
            if (ei == 0) {
                at += rl;
                continue;
            }
            /* Hide dot entries (cleaner `ls`; reachable by path). */
            if (nl == 1 && g_blk[at + 8u] == '.') {
                at += rl;
                continue;
            }
            if (nl == 2 && g_blk[at + 8u] == '.' &&
                g_blk[at + 9u] == '.') {
                at += rl;
                continue;
            }
            if (seen != index) {
                seen++;
                at += rl;
                continue;
            }
            if (nl > 255u) {
                return -NOVA_EIO;
            }
            for (k = 0; k < nl; k++) {
                name_out[k] = (char)g_blk[at + 8u + k];
            }
            name_out[k] = 0;
            return (int)k;
        }
    }
    return -NOVA_ENOENT;
}

static int ext_eq(const uint8_t *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != (uint8_t)b[i]) {
            return 0;
        }
    }
    return 1;
}

int ext_selftest(void) {
    /* 512B cap: bigger frames trip __chkstk_ms (no libgcc on link). */
    static uint8_t buf[512];
    uint32_t size = 0;
    int is_dir = 0;
    uint32_t got = 0;
    char name[FS_MAX_NAME];
    /* CONTRACT with tools/mkext4.py (asserted, not eyeballed). */
    if (ext_stat("/HELLO.TXT", &size, &is_dir) != 0 || size != 16u ||
        is_dir) {
        serial_puts("EXT-FAIL hello-stat\n");
        return -1;
    }
    if (ext_read_file("/HELLO.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 16u || !ext_eq(buf, "hello from ext4\n", 16u)) {
        serial_puts("EXT-FAIL hello-bytes\n");
        return -1;
    }
    /* Case-sensitive: lowercase misses (unlike FAT). */
    if (ext_stat("/hello.txt", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("EXT-FAIL case\n");
        return -1;
    }
    if (ext_stat("/DOCS", &size, &is_dir) != 0 || !is_dir) {
        serial_puts("EXT-FAIL docs-stat\n");
        return -1;
    }
    if (ext_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 15u || !ext_eq(buf, "ext4 nested ok\n", 15u)) {
        serial_puts("EXT-FAIL note-bytes\n");
        return -1;
    }
    {
        int hello = 0, docs = 0;
        for (uint32_t i = 0; i < 8u; i++) {
            int rc = ext_list_dir("/", i, name);
            uint32_t k;
            if (rc == -NOVA_ENOENT) {
                break;
            }
            if (rc < 0) {
                serial_puts("EXT-FAIL ls-err\n");
                return -1;
            }
            k = 0;
            while ("HELLO.TXT"[k] != 0 && "HELLO.TXT"[k] == name[k]) {
                k++;
            }
            if ("HELLO.TXT"[k] == 0 && name[k] == 0) {
                hello = 1;
            }
            k = 0;
            while ("DOCS"[k] != 0 && "DOCS"[k] == name[k]) {
                k++;
            }
            if ("DOCS"[k] == 0 && name[k] == 0) {
                docs = 1;
            }
        }
        if (!hello || !docs) {
            serial_puts("EXT-FAIL ls-names\n");
            return -1;
        }
    }
    if (ext_list_dir("/DOCS", 9, name) != -NOVA_ENOENT) {
        serial_puts("EXT-FAIL ls-end\n");
        return -1;
    }
    if (ext_stat("/NOPE.TXT", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("EXT-FAIL enoent\n");
        return -1;
    }
    if (ext_read_file("/DOCS", buf, sizeof(buf), &got) != -NOVA_EISDIR) {
        serial_puts("EXT-FAIL eisdir\n");
        return -1;
    }
    serial_puts("EXT-OK\n");
    return NOVA_ESUCCESS;
}
