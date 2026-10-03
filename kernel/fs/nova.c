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
static nova_write_sector_t g_ws;
static uint32_t g_blocks;
static uint32_t g_inotab;
static uint8_t g_blk[4096];
/* Journal state (Phase 7f-2) + second scratch (mutators build blocks
 * here while g_blk serves reads; no nesting, single-core). */
static uint32_t g_jstart;
static uint32_t g_jcount;
static uint32_t g_jcur;
static uint32_t g_seq;
static uint8_t g_scratch[4096];
/* Transaction staging pool (Phase 7f-2): each journal pair needs its
 * own buffer alive at commit time (slots stage immediately, so one
 * buffer per pair). 8 pairs cap all v0.1 transactions (data ≤3 +
 * bitmaps + itable + dir + SB copies). */
static uint8_t g_pool[8][4096];
static uint32_t g_bbmap;
static uint32_t g_ibmap;
static uint32_t g_free_blocks;
static uint32_t g_free_inodes;
static uint32_t g_inodes;

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
static uint32_t crc32c_update(uint32_t crc, const uint8_t *data,
                              uint32_t len) {
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
    return crc;
}

static uint32_t crc32c(const uint8_t *data, uint32_t len) {
    return crc32c_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
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

/* Whole-block sector write through the callback. */
static int write_block(uint32_t blk, const uint8_t *src) {
    uint32_t base = blk * NOVA_SPB;
    if (g_ws == 0 || src == 0) {
        return -NOVA_EINVAL;
    }
    if (blk >= g_blocks && g_blocks != 0) {
        return -NOVA_EIO;
    }
    for (uint32_t s = 0; s < NOVA_SPB; s++) {
        if (g_ws(base + s, src + s * 512u) != 0) {
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

/* ---------- journal (Phase 7f-2, ordered metadata) ----------
 * Transaction = descriptor + data blocks + commit, all inside the
 * journal area. Descriptor lists (home block, data crc) pairs; the
 * commit block is dedicated (single-sector atomic). Checkpoint copies
 * data home only after crc-verifying each staged block, so torn
 * journal writes are treated as absent. Cursor wraps between
 * transactions (checkpoint is synchronous: no GC needed). */

#define NOVA_J_MAGIC 0x4A524E4Cu
#define NOVA_C_MAGIC 0x434F4D54u
#define NOVA_JPAIR_MAX 8u

struct jpair {
    uint32_t home;
    const uint8_t *data; /* 4KB, valid through journal_commit */
};

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Validate a descriptor block: returns pair count, or 0 when invalid.
 * Layout: [0]=magic, [4]=seq, [8]=count, [12]=crc over [0..11] +
 * homes[16..] + crcs[...]; crc lives in sector 0 (atomic unit). */
static uint32_t jdesc_valid(const uint8_t *blk, uint32_t *seq_out) {
    uint32_t count;
    uint32_t crc;
    if (rd32(blk) != NOVA_J_MAGIC) {
        return 0;
    }
    count = rd32(blk + 8);
    if (count == 0 || count > NOVA_JPAIR_MAX) {
        return 0;
    }
    crc = crc32c_update(0xFFFFFFFFu, blk, 12);
    crc = crc32c_update(crc, blk + 16, count * 8u);
    crc ^= 0xFFFFFFFFu;
    if (crc != rd32(blk + 12)) {
        return 0;
    }
    if (seq_out != 0) {
        *seq_out = rd32(blk + 4);
    }
    return count;
}

static int jcommit_valid(uint32_t blk, uint32_t seq) {
    uint8_t sb[512];
    uint32_t base = blk * NOVA_SPB;
    /* Commit lives or dies on its first sector (atomic unit). */
    if (g_rs == 0) {
        return 0;
    }
    if (g_rs(base, sb) != 0) {
        return 0;
    }
    if (rd32(sb) != NOVA_C_MAGIC) {
        return 0;
    }
    if (rd32(sb + 4) != seq) {
        return 0;
    }
    return crc32c(sb, 8) == rd32(sb + 8);
}

/* Checkpoint one transaction: crc-verify each staged block, copy
 * home. Stops (leaves rest) on first mismatch. */
static void jcheckpoint(uint32_t desc_blk, uint32_t count) {
    uint8_t desc[512];
    uint32_t base = desc_blk * NOVA_SPB;
    if (g_rs(base, desc) != 0) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t home = rd32(desc + 16 + i * 8u);
        uint32_t want = rd32(desc + 16 + count * 8u + i * 4u);
        /* Staged data block i sits right after the descriptor. */
        if (read_block(desc_blk + 1u + i, g_scratch) != 0) {
            return;
        }
        if (crc32c(g_scratch, NOVA_BLOCKS) != want) {
            return;
        }
        if (write_block(home, g_scratch) != 0) {
            return;
        }
    }
}

/* Mount recovery: replay every complete committed transaction in
 * SEQUENCE order (idempotent: copies are exact). Scan order is NOT
 * time order once the cursor wraps: replaying by block would
 * resurrect deleted files (create replayed after its delete).
 * Collect + insertion-sort (<=64 entries), then replay. Advances the
 * cursor past the journal and the seq beyond anything seen. */
static void journal_recover(void) {
    /* Static arrays (no stack bloat, no heap this early). */
    static uint32_t r_desc[64];
    static uint32_t r_count[64];
    static uint32_t r_seq[64];
    uint32_t b = g_jstart;
    uint32_t end = g_jstart + g_jcount;
    uint32_t maxseq = 0;
    uint32_t n = 0;
    while (b < end && n < 64u) {
        uint32_t seq = 0;
        uint32_t count = 0;
        if (read_block(b, g_blk) != 0) {
            b++;
            continue;
        }
        count = jdesc_valid(g_blk, &seq);
        if (count == 0) {
            b++;
            continue;
        }
        if (b + 1u + count >= end) {
            break; /* descriptor claims past the area: stop */
        }
        if (seq > maxseq) {
            maxseq = seq;
        }
        if (jcommit_valid(b + 1u + count, seq)) {
            /* Insertion-sort by seq (journal is tiny). */
            uint32_t k = n;
            r_desc[n] = b;
            r_count[n] = count;
            r_seq[n] = seq;
            n++;
            while (k > 0 && r_seq[k] < r_seq[k - 1u]) {
                uint32_t t;
                t = r_seq[k];
                r_seq[k] = r_seq[k - 1u];
                r_seq[k - 1u] = t;
                t = r_desc[k];
                r_desc[k] = r_desc[k - 1u];
                r_desc[k - 1u] = t;
                t = r_count[k];
                r_count[k] = r_count[k - 1u];
                r_count[k - 1u] = t;
                k--;
            }
        }
        b += 2u + count;
    }
    for (uint32_t i = 0; i < n; i++) {
        jcheckpoint(r_desc[i], r_count[i]);
    }
    g_jcur = g_jstart;
    g_seq = (maxseq != 0) ? maxseq + 1u : 1u;
}

/* Commit n pairs atomically (1..JPAIR_MAX): stage data, descriptor,
 * commit, then checkpoint synchronously. Returns 0 or -errno. */
static int journal_commit(const struct jpair *pairs, uint32_t n) {
    uint32_t desc;
    uint32_t i;
    if (pairs == 0 || n == 0 || n > NOVA_JPAIR_MAX) {
        return -NOVA_EINVAL;
    }
    if (g_jcur + 2u + n > g_jstart + g_jcount) {
        g_jcur = g_jstart; /* wrap between transactions */
    }
    desc = g_jcur;
    for (i = 0; i < n; i++) {
        if (write_block(desc + 1u + i, pairs[i].data) != 0) {
            return -NOVA_EIO;
        }
    }
    /* Descriptor (fits sector 0: 16 + 8*24 = 208B). */
    for (i = 0; i < NOVA_BLOCKS; i++) {
        g_blk[i] = 0;
    }
    wr32(g_blk, NOVA_J_MAGIC);
    wr32(g_blk + 4, g_seq);
    wr32(g_blk + 8, n);
    for (i = 0; i < n; i++) {
        uint32_t c = crc32c(pairs[i].data, NOVA_BLOCKS);
        wr32(g_blk + 16 + i * 8u, pairs[i].home);
        wr32(g_blk + 16 + n * 8u + i * 4u, c);
    }
    {
        uint32_t crc = crc32c_update(0xFFFFFFFFu, g_blk, 12);
        crc = crc32c_update(crc, g_blk + 16, n * 8u);
        wr32(g_blk + 12, crc ^ 0xFFFFFFFFu);
    }
    if (write_block(desc, g_blk) != 0) {
        return -NOVA_EIO;
    }
    /* Commit record (dedicated block; only sector 0 matters). */
    for (i = 0; i < NOVA_BLOCKS; i++) {
        g_blk[i] = 0;
    }
    wr32(g_blk, NOVA_C_MAGIC);
    wr32(g_blk + 4, g_seq);
    wr32(g_blk + 8, crc32c(g_blk, 8));
    if (write_block(desc + 1u + n, g_blk) != 0) {
        return -NOVA_EIO;
    }
    jcheckpoint(desc, n);
    g_seq++;
    g_jcur = desc + 2u + n;
    return NOVA_ESUCCESS;
}

int nova_init(nova_read_sector_t rs, nova_write_sector_t ws) {
    uint32_t total = 0;
    uint32_t copy_blk = 0;
    const uint8_t *sb;
    if (rs == 0 || ws == 0) {
        return -NOVA_EINVAL;
    }
    g_rs = rs;
    g_ws = ws;
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
        copy_blk = raw_total - 1u;
    }
    g_blocks = total;
    /* Adopt layout pointers from the verified copy. */
    if (read_block(copy_blk, g_blk) != 0) {
        return -NOVA_EIO;
    }
    sb = g_blk + 1024;
    g_inotab = rd32(sb + 96);
    g_jstart = rd32(sb + 32);
    g_jcount = rd32(sb + 36);
    g_bbmap = rd32(sb + 100);
    g_ibmap = rd32(sb + 104);
    g_free_blocks = rd32(sb + 16);
    g_free_inodes = rd32(sb + 24);
    g_inodes = rd32(sb + 20);
    if (g_inotab == 0 || g_inotab >= total) {
        return -NOVA_EIO;
    }
    if (g_jstart == 0 || g_jcount == 0 ||
        g_jstart + g_jcount > total) {
        return -NOVA_EIO;
    }
    if (g_bbmap == 0 || g_bbmap >= total || g_ibmap == 0 ||
        g_ibmap >= total) {
        return -NOVA_EIO;
    }
    journal_recover();
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

/* ---------- allocator + mutators (Phase 7f-2) ----------
 * Bitmaps are ordinary blocks: read into a pool buffer, flip bits,
 * submit as journal pairs. SB free counts update in the SAME
 * transaction (both copies, same content buffer). Inode edits
 * recompute the @112 crc before pairing the table block. */

/* Find a zero bit in a bitmap block buffer (bits = blocks/inodes). */
static int bitmap_find(const uint8_t *map, uint32_t bits, uint32_t hint,
                       uint32_t *bit_out) {
    if (map == 0 || bit_out == 0 || bits == 0) {
        return -NOVA_EINVAL;
    }
    for (uint32_t n = 0; n < bits; n++) {
        uint32_t b = (hint + n) % bits;
        if ((map[b / 8u] & (uint8_t)(1u << (b % 8u))) == 0) {
            *bit_out = b;
            return NOVA_ESUCCESS;
        }
    }
    return -NOVA_ENOSPC;
}

static void bitmap_flip(uint8_t *map, uint32_t bit, int set) {
    if (set) {
        map[bit / 8u] = (uint8_t)(map[bit / 8u] |
                                  (1u << (bit % 8u)));
    } else {
        map[bit / 8u] = (uint8_t)(map[bit / 8u] &
                                  ~(1u << (bit % 8u)));
    }
}

/* Stage patched SB copies (free-count deltas) into a pool buffer.
 * Returns the buffer (both copies share content). */
static uint8_t *sb_stage(int32_t db, int32_t di) {
    uint8_t *sb;
    uint32_t fb, fi;
    if (read_block(0, g_scratch) != 0) {
        return 0;
    }
    sb = g_scratch + 1024;
    fb = rd32(sb + 16);
    fi = rd32(sb + 24);
    if (db < 0 && (uint32_t)(-db) > fb) {
        return 0;
    }
    if (di < 0 && (uint32_t)(-di) > fi) {
        return 0;
    }
    wr32(sb + 16, fb + (uint32_t)db);
    wr32(sb + 24, fi + (uint32_t)di);
    wr32(sb + 48, crc32c(sb, 48));
    return g_scratch;
}

/* Patch one inode inside a staged table-block buffer (crc fixed). */
static void itable_patch(uint8_t *tab, uint32_t ino, const uint8_t *in) {
    uint32_t off = ((ino - 1u) % NOVA_INODES_PER_BLOCK) *
                   NOVA_INODE_SIZE;
    uint8_t *e = tab + off;
    for (uint32_t i = 0; i < NOVA_INODE_SIZE; i++) {
        e[i] = in[i];
    }
    wr32(e + 112, crc32c(e, 112));
}

/* Build a fresh inode image (mode/size/links + single start extent
 * or empty). Checksum filled by itable_patch at stage time... note:
 * callers must crc before pairing (itable_patch does it). */
static void inode_build(uint8_t *in, uint16_t mode, uint32_t size,
                        uint16_t links, uint32_t first_blk) {
    for (uint32_t i = 0; i < NOVA_INODE_SIZE; i++) {
        in[i] = 0;
    }
    in[0] = (uint8_t)(mode & 0xFFu);
    in[1] = (uint8_t)(mode >> 8);
    wr32(in + 4, size);
    in[26] = (uint8_t)(links & 0xFFu);
    in[27] = (uint8_t)(links >> 8);
    wr32(in + 28, (size + 511u) / 512u);
    in[40] = 0x0Au;
    in[41] = 0xF3u; /* NOVA_EXT_MAGIC little-endian */
    if (first_blk != 0) {
        in[42] = 1; /* one extent */
        wr32(in + 48, first_blk);
        in[52] = 1; /* length lo */
        in[53] = 0;
    }
}

/* Entry writer into a staged dir block buffer (opaque name bytes).
 * Splitting variant for reusing a free slot: the entry takes need
 * bytes; a remainder >= 8 becomes a fresh free entry (a zero tail
 * would scan as corruption). Returns 0 or -EINVAL (need > slot). */
static int dir_put_slot(uint8_t *blk, uint32_t off, uint32_t slot_rl,
                        uint32_t ino, const uint8_t *name, uint32_t len,
                        uint8_t type) {
    uint32_t need = (8u + len + 3u) & ~3u;
    uint32_t i;
    uint32_t rem;
    if (need > slot_rl) {
        return -NOVA_EINVAL;
    }
    blk[off] = (uint8_t)(ino & 0xFFu);
    blk[off + 1u] = (uint8_t)((ino >> 8) & 0xFFu);
    blk[off + 2u] = (uint8_t)((ino >> 16) & 0xFFu);
    blk[off + 3u] = (uint8_t)((ino >> 24) & 0xFFu);
    blk[off + 4u] = (uint8_t)(need & 0xFFu);
    blk[off + 5u] = (uint8_t)(need >> 8);
    blk[off + 6u] = (uint8_t)len;
    blk[off + 7u] = type;
    for (i = 0; i < len; i++) {
        blk[off + 8u + i] = name[i];
    }
    for (; 8u + i < need; i++) {
        blk[off + 8u + i] = 0;
    }
    rem = slot_rl - need;
    if (rem >= 8u) {
        uint32_t ro = off + need;
        blk[ro] = 0;
        blk[ro + 1u] = 0;
        blk[ro + 2u] = 0;
        blk[ro + 3u] = 0;
        blk[ro + 4u] = (uint8_t)(rem & 0xFFu);
        blk[ro + 5u] = (uint8_t)(rem >> 8);
        blk[ro + 6u] = 0;
        blk[ro + 7u] = 0;
    } else if (rem != 0) {
        /* Absorb crumbs: entry stretches over the whole slot. */
        blk[off + 4u] = (uint8_t)(slot_rl & 0xFFu);
        blk[off + 5u] = (uint8_t)(slot_rl >> 8);
    }
    return NOVA_ESUCCESS;
}

/* Exact writer (fresh zeroed blocks + . / .. setup only). */
static void dir_put(uint8_t *blk, uint32_t off, uint32_t ino,
                    const uint8_t *name, uint32_t len, uint8_t type) {
    uint32_t rl = 8u + len;
    uint32_t i;
    rl = (rl + 3u) & ~3u;
    blk[off] = (uint8_t)(ino & 0xFFu);
    blk[off + 1u] = (uint8_t)((ino >> 8) & 0xFFu);
    blk[off + 2u] = (uint8_t)((ino >> 16) & 0xFFu);
    blk[off + 3u] = (uint8_t)((ino >> 24) & 0xFFu);
    blk[off + 4u] = (uint8_t)(rl & 0xFFu);
    blk[off + 5u] = (uint8_t)(rl >> 8);
    blk[off + 6u] = (uint8_t)len;
    blk[off + 7u] = type;
    for (i = 0; i < len; i++) {
        blk[off + 8u + i] = name[i];
    }
    for (; 8u + i < rl; i++) {
        blk[off + 8u + i] = 0;
    }
}

/* Find a free slot in a single-block dir's staged buffer (ino==0)
 * that fits need_rl bytes. v0.1 dirs stay one block (fixtures stay
 * tiny; growth is 7f-3). Returns the byte offset + slot size, or
 * -ENOSPC. */
static int dir_slot(const uint8_t *blk, uint32_t need_rl,
                    uint32_t *off_out, uint32_t *slot_rl_out) {
    uint32_t at = 0;
    if (blk == 0 || off_out == 0 || slot_rl_out == 0 ||
        need_rl < 8u || need_rl > NOVA_BLOCKS) {
        return -NOVA_EINVAL;
    }
    while (at + 8u <= NOVA_BLOCKS) {
        uint32_t rl = rd16(blk + at + 4u);
        if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
            return -NOVA_EIO;
        }
        if (rd32(blk + at) == 0 && rl >= need_rl) {
            *off_out = at;
            *slot_rl_out = rl;
            return NOVA_ESUCCESS;
        }
        at += rl;
    }
    return -NOVA_ENOSPC;
}

int nova_create_file(const char *path) {
    uint32_t dino;
    const char *last;
    uint32_t llen;
    uint8_t di[NOVA_INODE_SIZE];
    uint32_t dblk = 0;
    uint32_t nino = 0;
    uint32_t itab;
    uint32_t slot = 0;
    uint8_t newino[NOVA_INODE_SIZE];
    uint8_t *sbbuf;
    struct jpair pairs[8];
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t sz = 0;
    int rc;
    path = strip_nova(path);
    if (path == 0) {
        return -NOVA_EINVAL;
    }
    rc = split_parent(path, &dino, &last, &llen);
    if (rc != 0) {
        return rc;
    }
    if (llen == 0 || llen > 255u) {
        return -NOVA_EINVAL;
    }
    /* Refuse when anything already has the name. */
    rc = dir_find(dino, last, llen, &ei, &ft, &sz);
    if (rc == 0) {
        return -NOVA_EEXIST;
    }
    if (rc != -NOVA_ENOENT) {
        return rc;
    }
    if (inode_read(dino, di) != 0) {
        return -NOVA_EIO;
    }
    if (ino_block(di, 0, &dblk) != 0 || dblk == 0) {
        return -NOVA_EIO;
    }
    if (read_block(dblk, g_pool[0]) != 0) {
        return -NOVA_EIO;
    }
    {
        uint32_t slot_rl = 0;
        if (dir_slot(g_pool[0], ((8u + llen + 3u) & ~3u), &slot,
                     &slot_rl) != 0) {
            return -NOVA_ENOSPC;
        }
        /* Allocate the inode (bit set staged in pool[1]). */
        if (read_block(g_ibmap, g_pool[1]) != 0) {
            return -NOVA_EIO;
        }
        if (bitmap_find(g_pool[1], g_inodes, 1, &nino) != 0) {
            return -NOVA_ENOSPC;
        }
        bitmap_flip(g_pool[1], nino, 1);
        itab = g_inotab + nino / NOVA_INODES_PER_BLOCK;
        if (read_block(itab, g_pool[2]) != 0) {
            return -NOVA_EIO;
        }
        inode_build(newino, 0x81A4u, 0, 1, 0);
        itable_patch(g_pool[2], nino + 1u, newino);
        /* Dir entry (regular file; split keeps the scan valid). */
        if (dir_put_slot(g_pool[0], slot, slot_rl, nino + 1u,
                         (const uint8_t *)last, llen, 1) != 0) {
            return -NOVA_EIO;
        }
    }
    sbbuf = sb_stage(0, -1);
    if (sbbuf == 0) {
        return -NOVA_ENOSPC;
    }
    pairs[0].home = g_ibmap;
    pairs[0].data = g_pool[1];
    pairs[1].home = itab;
    pairs[1].data = g_pool[2];
    pairs[2].home = dblk;
    pairs[2].data = g_pool[0];
    pairs[3].home = 0;
    pairs[3].data = sbbuf;
    pairs[4].home = g_blocks - 1u;
    pairs[4].data = sbbuf;
    if (journal_commit(pairs, 5) != 0) {
        return -NOVA_EIO;
    }
    g_free_inodes--;
    return NOVA_ESUCCESS;
}

int nova_write_at(const char *path, uint32_t off, const uint8_t *src,
                  uint32_t len) {
    uint32_t dino;
    const char *last;
    uint32_t llen;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    uint8_t fi[NOVA_INODE_SIZE];
    uint32_t end;
    uint32_t have_b, need_b;
    uint32_t itab;
    struct jpair pairs[8];
    uint32_t np = 0;
    uint32_t grew = 0;
    int rc;
    path = strip_nova(path);
    if (path == 0 || (src == 0 && len != 0)) {
        return -NOVA_EINVAL;
    }
    if (len == 0) {
        return NOVA_ESUCCESS;
    }
    end = off + len;
    if (end < off || end > FS_MAX_FILE) {
        return -NOVA_ENOSPC;
    }
    /* Pair-budget bound: blocks touched per call (data + bitmap +
     * itable + 2 SB <= 4 + 4 = 8 pairs). Syscalls chunk at 512B. */
    {
        uint32_t span_b = (off % NOVA_BLOCKS + len + NOVA_BLOCKS - 1u) /
                          NOVA_BLOCKS;
        if (span_b > 4u) {
            return -NOVA_EINVAL;
        }
    }
    rc = split_parent(path, &dino, &last, &llen);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dino, last, llen, &ei, &ft, &size);
    if (rc != 0) {
        return rc;
    }
    if (ft == 2) {
        return -NOVA_EISDIR;
    }
    if (off > size) {
        return -NOVA_ENOSPC; /* no gap writes in v0.1 */
    }
    if (inode_read(ei, fi) != 0) {
        return -NOVA_EIO;
    }
    have_b = (size + NOVA_BLOCKS - 1u) / NOVA_BLOCKS;
    need_b = (end + NOVA_BLOCKS - 1u) / NOVA_BLOCKS;
    if (need_b > have_b) {
        /* Grow: alloc blocks (contiguity-preferred), extend/add
         * extents (4 inline max). Staged bitmap shared across the
         * whole growth (one pair). */
        uint32_t last_blk = 0;
        uint32_t count = rd16(fi + 42);
        if (count > 0) {
            uint32_t e = count - 1u;
            last_blk = rd32(fi + 48 + e * 8u) +
                       rd16(fi + 48 + e * 8u + 4u) - 1u;
        }
        if (read_block(g_bbmap, g_pool[0]) != 0) {
            return -NOVA_EIO;
        }
        for (uint32_t b = have_b; b < need_b; b++) {
            uint32_t nb = 0;
            uint32_t hint = (last_blk != 0) ? last_blk + 1u : 72u;
            if (bitmap_find(g_pool[0], g_blocks, hint, &nb) != 0) {
                return -NOVA_ENOSPC;
            }
            bitmap_flip(g_pool[0], nb, 1);
            /* Zero the fresh block through its pair buffer now
             * (unwritten regions must read zero). */
            if (np + 1u >= 8u) {
                return -NOVA_ENOSPC; /* pair budget */
            }
            for (uint32_t i = 0; i < NOVA_BLOCKS; i++) {
                g_pool[2 + np][i] = 0;
            }
            if (count > 0) {
                uint32_t e = count - 1u;
                uint32_t es = rd32(fi + 48 + e * 8u);
                uint32_t el = rd16(fi + 48 + e * 8u + 4u);
                if (es + el == nb) {
                    fi[48 + e * 8u + 4] =
                        (uint8_t)((el + 1u) & 0xFFu);
                    fi[48 + e * 8u + 5] =
                        (uint8_t)((el + 1u) >> 8);
                } else if (count < 4u) {
                    wr32(fi + 48 + count * 8u, nb);
                    fi[48 + count * 8u + 4] = 1;
                    fi[48 + count * 8u + 5] = 0;
                    fi[48 + count * 8u + 6] = 0;
                    fi[48 + count * 8u + 7] = 0;
                    count++;
                    fi[42] = (uint8_t)count;
                    fi[43] = 0;
                } else {
                    return -NOVA_ENOSPC;
                }
            } else {
                wr32(fi + 48, nb);
                fi[52] = 1;
                fi[53] = 0;
                fi[54] = 0;
                fi[55] = 0;
                count = 1;
                fi[42] = 1;
                fi[43] = 0;
            }
            pairs[np].home = nb;
            pairs[np].data = g_pool[2 + np];
            np++;
            last_blk = nb;
            grew++;
        }
        pairs[np].home = g_bbmap;
        pairs[np].data = g_pool[0];
        np++;
    }
    /* Data: merge each affected block (read home or staged zeros). */
    {
        uint32_t pos = off;
        uint32_t left = len;
        while (left > 0) {
            uint32_t bi = pos / NOVA_BLOCKS;
            uint32_t bo = pos % NOVA_BLOCKS;
            uint32_t n = NOVA_BLOCKS - bo;
            uint32_t dblk = 0;
            uint8_t *tgt;
            if (n > left) {
                n = left;
            }
            if (ino_block(fi, bi, &dblk) != 0 || dblk == 0) {
                return -NOVA_EIO;
            }
            /* Staged fresh blocks are pairs[0..grew): reuse the
             * staged buffer instead of re-reading. */
            tgt = 0;
            for (uint32_t i = 0; i < grew; i++) {
                if (pairs[i].home == dblk) {
                    tgt = (uint8_t *)pairs[i].data;
                    break;
                }
            }
            if (tgt == 0) {
                if (np >= 8u) {
                    return -NOVA_ENOSPC;
                }
                tgt = g_pool[2 + np];
                if (read_block(dblk, tgt) != 0) {
                    return -NOVA_EIO;
                }
                pairs[np].home = dblk;
                pairs[np].data = tgt;
                np++;
            }
            for (uint32_t i = 0; i < n; i++) {
                tgt[bo + i] = src[i];
            }
            src += n;
            pos += n;
            left -= n;
        }
    }
    /* Inode size (+ table block) and SB counts when grew. */
    itab = g_inotab + (ei - 1u) / NOVA_INODES_PER_BLOCK;
    if (read_block(itab, g_pool[1]) != 0) {
        return -NOVA_EIO;
    }
    if (end > size) {
        wr32(fi + 4, end);
        wr32(fi + 28, (end + 511u) / 512u);
    }
    itable_patch(g_pool[1], ei, fi);
    pairs[np].home = itab;
    pairs[np].data = g_pool[1];
    np++;
    if (grew != 0) {
        uint8_t *sbbuf = sb_stage(-(int32_t)grew, 0);
        if (sbbuf == 0) {
            return -NOVA_ENOSPC;
        }
        pairs[np].home = 0;
        pairs[np].data = sbbuf;
        np++;
        pairs[np].home = g_blocks - 1u;
        pairs[np].data = sbbuf;
        np++;
    }
    if (np > 8u) {
        return -NOVA_ENOSPC;
    }
    if (journal_commit(pairs, np) != 0) {
        return -NOVA_EIO;
    }
    g_free_blocks -= grew;
    return NOVA_ESUCCESS;
}

int nova_delete(const char *path) {
    uint32_t dino;
    const char *last;
    uint32_t llen;
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t size = 0;
    uint8_t fi[NOVA_INODE_SIZE];
    uint32_t itab;
    uint32_t ddblk = 0;
    uint32_t at = 0;
    uint32_t freed_total = 0;
    uint8_t *sbbuf;
    struct jpair pairs[8];
    int rc;
    int found = 0;
    path = strip_nova(path);
    if (path == 0) {
        return -NOVA_EINVAL;
    }
    rc = split_parent(path, &dino, &last, &llen);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dino, last, llen, &ei, &ft, &size);
    if (rc != 0) {
        return rc;
    }
    if (ft == 2) {
        return -NOVA_EISDIR;
    }
    if (inode_read(ei, fi) != 0) {
        return -NOVA_EIO;
    }
    /* Count data blocks first (SB delta is relative to DISK state). */
    {
        uint32_t count = rd16(fi + 42);
        uint32_t k = 0;
        for (uint32_t e = 0; e < count; e++) {
            k += rd16(fi + 48 + e * 8u + 4u);
        }
        if (k > 0x7FFFFFFFu) {
            return -NOVA_EIO;
        }
        freed_total = k;
        /* Free data blocks (single staged bitmap buffer). */
        if (read_block(g_bbmap, g_pool[0]) != 0) {
            return -NOVA_EIO;
        }
        {
            uint32_t freed = 0;
            for (uint32_t e = 0; e < count && freed < k; e++) {
                uint32_t bs = rd32(fi + 48 + e * 8u);
                uint32_t bl = rd16(fi + 48 + e * 8u + 4u);
                for (uint32_t j = 0; j < bl; j++) {
                    bitmap_flip(g_pool[0], bs + j, 0);
                    freed++;
                }
            }
        }
    }
    pairs[0].home = g_bbmap;
    pairs[0].data = g_pool[0];
    /* Free the inode (staged bitmap + zeroed table entry). */
    if (read_block(g_ibmap, g_pool[1]) != 0) {
        return -NOVA_EIO;
    }
    bitmap_flip(g_pool[1], ei - 1u, 0);
    pairs[1].home = g_ibmap;
    pairs[1].data = g_pool[1];
    itab = g_inotab + (ei - 1u) / NOVA_INODES_PER_BLOCK;
    if (read_block(itab, g_pool[2]) != 0) {
        return -NOVA_EIO;
    }
    {
        uint32_t off = ((ei - 1u) % NOVA_INODES_PER_BLOCK) *
                       NOVA_INODE_SIZE;
        for (uint32_t i = 0; i < NOVA_INODE_SIZE; i++) {
            g_pool[2][off + i] = 0;
        }
    }
    pairs[2].home = itab;
    pairs[2].data = g_pool[2];
    /* Clear the dir entry (ino=0 keeps rec_len for scan progress). */
    if (inode_read(dino, fi) != 0) {
        return -NOVA_EIO;
    }
    if (ino_block(fi, 0, &ddblk) != 0 || ddblk == 0) {
        return -NOVA_EIO;
    }
    if (read_block(ddblk, g_pool[3]) != 0) {
        return -NOVA_EIO;
    }
    while (at + 8u <= NOVA_BLOCKS && !found) {
        uint32_t cei = rd32(g_pool[3] + at);
        uint32_t rl = rd16(g_pool[3] + at + 4u);
        uint32_t nl = g_pool[3][at + 6u];
        if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
            return -NOVA_EIO;
        }
        if (cei != 0 && nl == llen) {
            uint32_t k = 0;
            while (k < llen &&
                   g_pool[3][at + 8u + k] == (uint8_t)last[k]) {
                k++;
            }
            if (k == llen) {
                g_pool[3][at] = 0;
                g_pool[3][at + 1u] = 0;
                g_pool[3][at + 2u] = 0;
                g_pool[3][at + 3u] = 0;
                found = 1;
                break;
            }
        }
        at += rl;
    }
    if (!found) {
        return -NOVA_ENOENT;
    }
    pairs[3].home = ddblk;
    pairs[3].data = g_pool[3];
    sbbuf = sb_stage((int32_t)freed_total, 1);
    if (sbbuf == 0) {
        return -NOVA_ENOSPC;
    }
    pairs[4].home = 0;
    pairs[4].data = sbbuf;
    pairs[5].home = g_blocks - 1u;
    pairs[5].data = sbbuf;
    if (journal_commit(pairs, 6) != 0) {
        return -NOVA_EIO;
    }
    g_free_blocks += freed_total;
    g_free_inodes++;
    return NOVA_ESUCCESS;
}

int nova_mkdir(const char *path) {
    uint32_t dino;
    const char *last;
    uint32_t llen;
    uint8_t di[NOVA_INODE_SIZE];
    uint32_t ddblk = 0;
    uint32_t at = 0;
    uint32_t nino = 0;
    uint32_t itab;
    uint32_t nblk = 0;
    uint32_t pitab;
    uint8_t newino[NOVA_INODE_SIZE];
    uint8_t *sbbuf;
    struct jpair pairs[8];
    uint32_t ei = 0;
    uint8_t ft = 0;
    uint32_t sz = 0;
    int rc;
    int slot_found = 0;
    uint32_t slot_off = 0;
    uint32_t slot_rl = 0;
    path = strip_nova(path);
    if (path == 0) {
        return -NOVA_EINVAL;
    }
    rc = split_parent(path, &dino, &last, &llen);
    if (rc != 0) {
        return rc;
    }
    if (llen == 0 || llen > 255u) {
        return -NOVA_EINVAL;
    }
    if (dir_find(dino, last, llen, &ei, &ft, &sz) == 0) {
        return -NOVA_EEXIST;
    }
    /* Allocate inode + one data block. */
    if (read_block(g_ibmap, g_pool[0]) != 0) {
        return -NOVA_EIO;
    }
    if (bitmap_find(g_pool[0], g_inodes, 1, &nino) != 0) {
        return -NOVA_ENOSPC;
    }
    bitmap_flip(g_pool[0], nino, 1);
    if (read_block(g_bbmap, g_pool[1]) != 0) {
        return -NOVA_EIO;
    }
    {
        uint32_t b = 0;
        if (bitmap_find(g_pool[1], g_blocks, 72u, &b) != 0) {
            return -NOVA_ENOSPC;
        }
        bitmap_flip(g_pool[1], b, 1);
        nblk = b;
    }
    /* Directory block content (. / .. + slack slots for future
     * creates; a packed dir would ENOSPC the first child, like the
     * fixture taught us). Last slot stretched (scan rule: no zero
     * tail may remain). */
    for (uint32_t i = 0; i < NOVA_BLOCKS; i++) {
        g_pool[2][i] = 0;
    }
    dir_put(g_pool[2], 0, nino + 1u, (const uint8_t *)".", 1, 2);
    dir_put(g_pool[2], 12, dino, (const uint8_t *)"..", 2, 2);
    {
        /* 24 + k*32 lands exactly (4096-24 = 127*32+8): the final
         * 8B slot always fits. The at+8 guard keeps rem >= 8, so the
         * rem-32 subtraction below can never underflow (an underflow
         * here once overflowed into the staged itable buffer). */
        uint32_t at = 24;
        while (at + 8u <= NOVA_BLOCKS) {
            uint32_t rem = NOVA_BLOCKS - at;
            /* Absorb crumbs (would leave <8): the last slot always
             * reaches end of block (scan rule). */
            uint32_t rl = (rem < 32u || rem - 32u < 8u) ? rem : 32u;
            g_pool[2][at] = 0;
            g_pool[2][at + 1u] = 0;
            g_pool[2][at + 2u] = 0;
            g_pool[2][at + 3u] = 0;
            g_pool[2][at + 4u] = (uint8_t)(rl & 0xFFu);
            g_pool[2][at + 5u] = (uint8_t)(rl >> 8);
            g_pool[2][at + 6u] = 0;
            g_pool[2][at + 7u] = 0;
            at += rl;
        }
    }
    /* Inode (dir, size 1 block, links 2, one extent). Reuses the
     * single table block when shared with the parent (fixture). */
    itab = g_inotab + nino / NOVA_INODES_PER_BLOCK;
    if (read_block(itab, g_pool[3]) != 0) {
        return -NOVA_EIO;
    }
    inode_build(newino, 0x41EDu, NOVA_BLOCKS, 2, nblk);
    itable_patch(g_pool[3], nino + 1u, newino);
    /* Parent slot + parent link count. */
    if (inode_read(dino, di) != 0) {
        return -NOVA_EIO;
    }
    if (ino_block(di, 0, &ddblk) != 0 || ddblk == 0) {
        return -NOVA_EIO;
    }
    pitab = g_inotab + (dino - 1u) / NOVA_INODES_PER_BLOCK;
    if (pitab == itab) {
        /* Same table block: patch into the staged copy. */
        uint32_t off = ((dino - 1u) % NOVA_INODES_PER_BLOCK) *
                       NOVA_INODE_SIZE;
        uint8_t pino[NOVA_INODE_SIZE];
        uint32_t links;
        for (uint32_t i = 0; i < NOVA_INODE_SIZE; i++) {
            pino[i] = g_pool[3][off + i];
        }
        links = rd16(pino + 26) + 1u;
        pino[26] = (uint8_t)(links & 0xFFu);
        pino[27] = (uint8_t)(links >> 8);
        itable_patch(g_pool[3], dino, pino);
    } else {
        return -NOVA_EIO; /* multi-block table: 7f-3 */
    }
    if (read_block(ddblk, g_pool[4]) != 0) {
        return -NOVA_EIO;
    }
    {
        uint32_t need = (8u + llen + 3u) & ~3u;
        while (at + 8u <= NOVA_BLOCKS) {
            uint32_t rl = rd16(g_pool[4] + at + 4u);
            if (rl < 8u || at + rl > NOVA_BLOCKS || rl == 0) {
                return -NOVA_EIO;
            }
            if (rd32(g_pool[4] + at) == 0 && rl >= need) {
                slot_found = 1;
                slot_off = at;
                slot_rl = rl;
                break;
            }
            at += rl;
        }
    }
    if (!slot_found) {
        return -NOVA_ENOSPC; /* v0.1 single-block dirs */
    }
    if (dir_put_slot(g_pool[4], slot_off, slot_rl, nino + 1u,
                     (const uint8_t *)last, llen, 2) != 0) {
        return -NOVA_EIO;
    }
    sbbuf = sb_stage(-1, -1);
    if (sbbuf == 0) {
        return -NOVA_ENOSPC;
    }
    pairs[0].home = g_ibmap;
    pairs[0].data = g_pool[0];
    pairs[1].home = g_bbmap;
    pairs[1].data = g_pool[1];
    pairs[2].home = nblk;
    pairs[2].data = g_pool[2];
    pairs[3].home = itab;
    pairs[3].data = g_pool[3];
    pairs[4].home = ddblk;
    pairs[4].data = g_pool[4];
    pairs[5].home = 0;
    pairs[5].data = sbbuf;
    pairs[6].home = g_blocks - 1u;
    pairs[6].data = sbbuf;
    if (journal_commit(pairs, 7) != 0) {
        return -NOVA_EIO;
    }
    g_free_blocks--;
    g_free_inodes--;
    return NOVA_ESUCCESS;
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
    /* Write round-trips on scratch files (hermetic: deleted after).
     * Idempotent across boots (VDIs persist): clear leftovers first,
     * tolerate an existing /ND (verify it is a dir). */
    nova_delete("/W.TXT");
    nova_delete("/BIG.BIN");
    if (nova_create_file("/W.TXT") != 0) {
        serial_puts("NOVA-FAIL create\n");
        return -1;
    }
    if (nova_create_file("/W.TXT") != -NOVA_EEXIST) {
        serial_puts("NOVA-FAIL exist\n");
        return -1;
    }
    {
        uint32_t i;
        for (i = 0; i < 100u; i++) {
            buf[i] = (uint8_t)((i * 7u + 3u) & 0xFFu);
        }
        if (nova_write_at("/W.TXT", 0, buf, 100u) != 0) {
            serial_puts("NOVA-FAIL write\n");
            return -1;
        }
        for (i = 0; i < 512u; i++) {
            buf[i] = 0;
        }
        if (nova_read_file("/W.TXT", buf, sizeof(buf), &got) != 0 ||
            got != 100u) {
            serial_puts("NOVA-FAIL reread\n");
            return -1;
        }
        for (i = 0; i < 100u; i++) {
            if (buf[i] != (uint8_t)((i * 7u + 3u) & 0xFFu)) {
                break;
            }
        }
        if (i != 100u) {
            serial_puts("NOVA-FAIL bytes\n");
            return -1;
        }
    }
    if (nova_create_file("/BIG.BIN") != 0) {
        serial_puts("NOVA-FAIL big-create\n");
        return -1;
    }
    {
        uint32_t i;
        for (i = 0; i < 512u; i++) {
            buf[i] = (uint8_t)(i & 0xFFu);
        }
        for (uint32_t off = 0; off < 3000u; off += 512u) {
            uint32_t n = 3000u - off;
            if (n > 512u) {
                n = 512u;
            }
            if (nova_write_at("/BIG.BIN", off, buf, n) != 0) {
                serial_puts("NOVA-FAIL big-write\n");
                return -1;
            }
        }
    }
    if (nova_stat("/BIG.BIN", &size, &is_dir) != 0 || size != 3000u ||
        is_dir) {
        serial_puts("NOVA-FAIL big-size\n");
        return -1;
    }
    {
        int mrc = nova_mkdir("/ND");
        if (mrc != 0 && mrc != -NOVA_EEXIST) {
            serial_puts("NOVA-FAIL mkdir\n");
            return -1;
        }
    }
    if (nova_mkdir("/ND") != -NOVA_EEXIST) {
        serial_puts("NOVA-FAIL mkdir-exist\n");
        return -1;
    }
    if (nova_stat("/ND", &size, &is_dir) != 0 || !is_dir) {
        serial_puts("NOVA-FAIL mkdir-stat\n");
        return -1;
    }
    if (nova_delete("/W.TXT") != 0 || nova_delete("/BIG.BIN") != 0) {
        serial_puts("NOVA-FAIL delete\n");
        return -1;
    }
    if (nova_stat("/W.TXT", &size, &is_dir) != -NOVA_ENOENT ||
        nova_stat("/BIG.BIN", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("NOVA-FAIL gone\n");
        return -1;
    }
    if (nova_delete("/W.TXT") != -NOVA_ENOENT) {
        serial_puts("NOVA-FAIL del-missing\n");
        return -1;
    }
    serial_puts("NOVA-OK\n");
    return NOVA_ESUCCESS;
}
