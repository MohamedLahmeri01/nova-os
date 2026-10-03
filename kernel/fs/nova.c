/* NOVA OS native filesystem reader (Phase 7f-1, read-only).
 * Follows docs/NOVA-FS.md v0.1: parsed (not assumed) superblock with
 * copy fallback, per-inode checksums, extent-map data reads, linear
 * dir scans with rec_len progress enforcement. Refusal over guessing
 * throughout (bad magic/checksum/feature = -EIO, never a panic).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "fs/nova.h"
#include "fs/fs.h"
#include "syscall/syscall.h"

static nova_read_sector_t g_rs;
static uint32_t g_blocks;
static uint32_t g_inotab;
static uint8_t g_blk[4096];

#define NOVA_BLOCKS 4096u
#define NOVA_SPB (NOVA_BLOCKS / 512u)
#define NOVA_INODE_SIZE 128u
#define NOVA_INODES_PER_BLOCK (NOVA_BLOCKS / NOVA_INODE_SIZE)
#define NOVA_ROOT_INO 2u
#define NOVA_WALK_MAX 64u
#define NOVA_EXT_MAGIC 0xF30Au

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* crc32c (Castagnoli, reflected): bitwise (no 1KB table; correctness
 * first per the design's open question 1 — blocks are 4KB). */
static uint32_t crc32c(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0x82F63B78u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static int read_block(uint32_t blk, uint8_t *dst) {
    uint32_t base = blk * NOVA_SPB;
    if (g_rs == 0 || dst == 0) {
        return -NOVA_EINVAL;
    }
    if (blk >= g_blocks && g_blocks != 0) {
        return -NOVA_EIO;
    }
    for (uint32_t s = 0; s < NOVA_SPB; s++) {
        if (g_rs(base + s, dst + s * 512u) != 0) {
            return -NOVA_EIO;
        }
    }
    return NOVA_ESUCCESS;
}

/* Verify one superblock copy (at block-relative byte 1024). */
static int sb_check(uint32_t blk, uint32_t *total_out) {
    const uint8_t *sb;
    uint32_t total, feat;
    if (read_block(blk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    sb = g_blk + 1024;
    for (uint32_t i = 0; i < 8u; i++) {
        if (sb[i] != (uint8_t)"NOVAFS01"[i]) {
            return -NOVA_EIO;
        }
    }
    if (rd32(sb + 8) != NOVA_BLOCKS) {
        return -NOVA_EIO;
    }
    total = rd32(sb + 12);
    if (total == 0) {
        return -NOVA_EIO;
    }
    if (rd32(sb + 28) != NOVA_ROOT_INO) {
        return -NOVA_EIO;
    }
    feat = rd32(sb + 44);
    if (feat != 0) {
        return -NOVA_EIO; /* unknown features: refuse */
    }
    if (crc32c(sb, 48) != rd32(sb + 48)) {
        return -NOVA_EIO;
    }
    if (total_out != 0) {
        *total_out = total;
    }
    return NOVA_ESUCCESS;
}

int nova_init(nova_read_sector_t rs) {
    uint32_t total = 0;
    const uint8_t *sb;
    if (rs == 0) {
        return -NOVA_EINVAL;
    }
    g_rs = rs;
    g_blocks = 0; /* unknown until SB parses: allow block 0 read */
    if (sb_check(0, &total) != 0) {
        /* Copy 0 failed: locate copy 1 via copy 0's raw total field
         * (untrusted pointer, but copy 1 gets FULL verification:
         * magic + features + checksum, so garbage just fails). */
        uint32_t raw_total = 0;
        if (read_block(0, g_blk) == 0) {
            raw_total = rd32(g_blk + 1024 + 12);
        }
        if (raw_total == 0 ||
            sb_check(raw_total - 1u, &total) != 0) {
            return -NOVA_EIO;
        }
        /* Adopted from copy 1: re-read it as the layout source. */
        if (read_block(raw_total - 1u, g_blk) != 0) {
            return -NOVA_EIO;
        }
        g_blocks = total;
        sb = g_blk + 1024;
        g_inotab = rd32(sb + 96);
        if (g_inotab == 0 || g_inotab >= total) {
            return -NOVA_EIO;
        }
        return NOVA_ESUCCESS;
    }
    g_blocks = total;
    /* Adopt layout pointers from the verified copy. */
    if (read_block(0, g_blk) != 0) {
        return -NOVA_EIO;
    }
    sb = g_blk + 1024;
    g_inotab = rd32(sb + 96);
    if (g_inotab == 0 || g_inotab >= total) {
        return -NOVA_EIO;
    }
    return NOVA_ESUCCESS;
}

/* Load inode `ino` into a 128B buffer + verify its checksum. */
static int inode_read(uint32_t ino, uint8_t *out) {
    uint32_t idx;
    uint32_t blk;
    uint32_t off;
    if (out == 0 || ino < 1) {
        return -NOVA_EINVAL;
    }
    idx = ino - 1u;
    blk = g_inotab + idx / NOVA_INODES_PER_BLOCK;
    off = (idx % NOVA_INODES_PER_BLOCK) * NOVA_INODE_SIZE;
    if (read_block(blk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    for (uint32_t i = 0; i < NOVA_INODE_SIZE; i++) {
        out[i] = g_blk[off + i];
    }
    if (crc32c(out, 112) != rd32(out + 112)) {
        return -NOVA_EIO;
    }
    return NOVA_ESUCCESS;
}

static int ino_is_dir(const uint8_t *ino) {
    return (rd16(ino) & 0xF000u) == 0x4000u;
}

static uint32_t ino_size(const uint8_t *ino) {
    return rd32(ino + 4);
}

/* Map file-block index i through the inline extent list (depth 0
 * only in v0.1; nonzero depth is EIO, not silent). */
static int ino_block(const uint8_t *ino, uint32_t i, uint32_t *out) {
    uint16_t count;
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    if (rd16(ino + 40) != NOVA_EXT_MAGIC) {
        return -NOVA_EIO;
    }
    count = rd16(ino + 42);
    if (rd16(ino + 44) != 0 || count > 4u) {
        return -NOVA_EIO;
    }
    for (uint32_t e = 0; e < count; e++) {
        uint32_t len = rd16(ino + 48 + e * 8u + 4u);
        if (i < len) {
            *out = rd32(ino + 48 + e * 8u) + i;
            return NOVA_ESUCCESS;
        }
        i -= len;
    }
    *out = 0; /* sparse hole past the last extent */
    return NOVA_ESUCCESS;
}

/* Path convention: absolute paths with an optional /nova prefix. */
static const char *strip_nova(const char *path) {
    if (path != 0 && path[0] == '/' && path[1] == 'n' &&
        path[2] == 'o' && path[3] == 'v' && path[4] == 'a' &&
        (path[5] == 0 || path[5] == '/')) {
        if (path[5] == 0) {
            return "/";
        }
        return path + 5;
    }
    return path;
}

/* Resolve a dir path to its inode number ("" or "/" = root).
 * Dot/dotdot via an explicit inode stack (no recursion). */
static int dir_ino(const char *path, uint32_t *out) {
    uint32_t stack[FS_MAX_DEPTH + 1u];
    uint32_t depth = 0;
    const char *p;
    uint8_t ino[NOVA_INODE_SIZE];
    if (path == 0 || out == 0) {
        return -NOVA_EINVAL;
    }
    stack[0] = NOVA_ROOT_INO;
    p = path;
    while (*p == '/') {
        p++;
    }
    if (*p == 0) {
        *out = NOVA_ROOT_INO;
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
            uint32_t dblk = 0;
            uint32_t at = 0;
            if (depth >= FS_MAX_DEPTH) {
                return -NOVA_ENAMETOOLONG;
            }
            if (inode_read(stack[depth], ino) != 0) {
                return -NOVA_EIO;
            }
            if (!ino_is_dir(ino)) {
                return -NOVA_ENOTDIR;
            }
            /* Single-extent dirs in v0.1 (block 0 of the map). */
            if (ino_block(ino, 0, &dblk) != 0 || dblk == 0) {
                return -NOVA_EIO;
            }
            if (read_block(dblk, g_blk) != 0) {
                return -NOVA_EIO;
            }
            while (at + 8u <= NOVA_BLOCKS) {
                uint32_t ei = rd32(g_blk + at);
                uint32_t rl = rd16(g_blk + at + 4u);
                uint32_t nl = g_blk[at + 6u];
                if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
                    return -NOVA_EIO;
                }
                if (ei != 0 && nl == len) {
                    uint32_t k = 0;
                    while (k < len &&
                           g_blk[at + 8u + k] == (uint8_t)s[k]) {
                        k++;
                    }
                    if (k == len) {
                        uint8_t ci[NOVA_INODE_SIZE];
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
                at += rl;
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

/* Find an entry by exact name inside a dir inode: number + type +
 * size. Zero ino marks end-of-scan (empty slots skipped). */
static int dir_find(uint32_t dino, const char *name, uint32_t len,
                    uint32_t *ino_out, uint8_t *type_out,
                    uint32_t *size_out) {
    uint8_t di[NOVA_INODE_SIZE];
    uint32_t dblk = 0;
    uint32_t at = 0;
    if (name == 0 || len == 0 || len > 255u) {
        return -NOVA_EINVAL;
    }
    if (inode_read(dino, di) != 0) {
        return -NOVA_EIO;
    }
    if (!ino_is_dir(di)) {
        return -NOVA_ENOTDIR;
    }
    if (ino_block(di, 0, &dblk) != 0 || dblk == 0) {
        return -NOVA_EIO;
    }
    if (read_block(dblk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    while (at + 8u <= NOVA_BLOCKS) {
        uint32_t ei = rd32(g_blk + at);
        uint32_t rl = rd16(g_blk + at + 4u);
        uint32_t nl = g_blk[at + 6u];
        uint8_t ft = g_blk[at + 7u];
        if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
            return -NOVA_EIO;
        }
        if (ei != 0 && nl == len) {
            uint32_t k = 0;
            while (k < len && g_blk[at + 8u + k] == (uint8_t)name[k]) {
                k++;
            }
            if (k == len) {
                uint8_t ci[NOVA_INODE_SIZE];
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
    return -NOVA_ENOENT;
}

/* Split "/a/b/C" into parent-dir inode + final name span. */
static int split_parent(const char *path, uint32_t *dino_out,
                        const char **last_out, uint32_t *len_out) {
    const char *last = 0;
    const char *p;
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
        /* Preserve ENOTDIR (through-a-file) vs ENOENT. */
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

int nova_stat(const char *path, uint32_t *size_out, int *is_dir_out) {
    uint32_t dino;
    const char *last;
    uint32_t len;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    int rc;
    path = strip_nova(path);
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

int nova_read_file(const char *path, uint8_t *dst, uint32_t max,
                   uint32_t *len_out) {
    uint32_t dino;
    const char *last;
    uint32_t len;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    uint8_t fi[NOVA_INODE_SIZE];
    uint32_t got = 0;
    uint32_t steps = 0;
    int rc;
    path = strip_nova(path);
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
    while (got < size && steps++ < NOVA_WALK_MAX) {
        uint32_t bi = got / NOVA_BLOCKS;
        uint32_t bo = got % NOVA_BLOCKS;
        uint32_t n = NOVA_BLOCKS - bo;
        uint32_t dblk = 0;
        if (n > size - got) {
            n = size - got;
        }
        if (ino_block(fi, bi, &dblk) != 0) {
            return -NOVA_EIO;
        }
        if (dblk == 0) {
            /* Sparse hole: zeros (no disk read). */
            for (uint32_t i = 0; i < n; i++) {
                dst[got + i] = 0;
            }
            got += n;
            continue;
        }
        if (read_block(dblk, g_blk) != 0) {
            return -NOVA_EIO;
        }
        for (uint32_t i = 0; i < n; i++) {
            dst[got + i] = g_blk[bo + i];
        }
        got += n;
    }
    if (got != size) {
        return -NOVA_EIO;
    }
    *len_out = got;
    return NOVA_ESUCCESS;
}

int nova_list_dir(const char *path, uint32_t index, char *name_out) {
    uint32_t dino;
    uint8_t di[NOVA_INODE_SIZE];
    uint32_t dblk = 0;
    uint32_t at = 0;
    uint32_t seen = 0;
    int rc;
    path = strip_nova(path);
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
    if (ino_block(di, 0, &dblk) != 0 || dblk == 0) {
        return -NOVA_EIO;
    }
    if (read_block(dblk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    while (at + 8u <= NOVA_BLOCKS) {
        uint32_t ei = rd32(g_blk + at);
        uint32_t rl = rd16(g_blk + at + 4u);
        uint32_t nl = g_blk[at + 6u];
        uint32_t k;
        if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
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
    return -NOVA_ENOENT;
}

static int nova_eq(const uint8_t *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != (uint8_t)b[i]) {
            return 0;
        }
    }
    return 1;
}

int nova_selftest(void) {
    /* 512B cap: bigger frames trip __chkstk_ms (no libgcc on link). */
    static uint8_t buf[512];
    uint32_t size = 0;
    int is_dir = 0;
    uint32_t got = 0;
    char name[FS_MAX_NAME];
    /* CONTRACT with tools/mknova.py (asserted, not eyeballed). */
    if (nova_stat("/HELLO.TXT", &size, &is_dir) != 0 || size != 16u ||
        is_dir) {
        serial_puts("NOVA-FAIL hello-stat\n");
        return -1;
    }
    if (nova_read_file("/HELLO.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 16u || !nova_eq(buf, "hello from nova\n", 16u)) {
        serial_puts("NOVA-FAIL hello-bytes\n");
        return -1;
    }
    /* Lowercase misses (names are opaque bytes, no folding). */
    if (nova_stat("/hello.txt", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("NOVA-FAIL case\n");
        return -1;
    }
    if (nova_stat("/DOCS", &size, &is_dir) != 0 || !is_dir) {
        serial_puts("NOVA-FAIL docs-stat\n");
        return -1;
    }
    if (nova_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 15u || !nova_eq(buf, "nova nested ok\n", 15u)) {
        serial_puts("NOVA-FAIL note-bytes\n");
        return -1;
    }
    {
        int hello = 0, docs = 0;
        for (uint32_t i = 0; i < 8u; i++) {
            int rc = nova_list_dir("/", i, name);
            uint32_t k;
            if (rc == -NOVA_ENOENT) {
                break;
            }
            if (rc < 0) {
                serial_puts("NOVA-FAIL ls-err\n");
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
            serial_puts("NOVA-FAIL ls-names\n");
            return -1;
        }
    }
    if (nova_list_dir("/DOCS", 9, name) != -NOVA_ENOENT) {
        serial_puts("NOVA-FAIL ls-end\n");
        return -1;
    }
    if (nova_stat("/NOPE.TXT", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("NOVA-FAIL enoent\n");
        return -1;
    }
    if (nova_read_file("/DOCS", buf, sizeof(buf), &got) != -NOVA_EISDIR) {
        serial_puts("NOVA-FAIL eisdir\n");
        return -1;
    }
    serial_puts("NOVA-OK\n");
    return NOVA_ESUCCESS;
}
