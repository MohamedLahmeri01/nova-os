/* NOVA OS FAT32 reader (Phase 7c, read-only).
 * One static scratch sector; every parse step revalidates geometry
 * (a corrupt disk returns errno, never loops forever: cluster chains
 * are step-capped, depths capped). Name matching uppercases ASCII
 * a-z; 8.3 split on the single dot (multi-dot names miss = ENOENT).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "fs/fat32.h"
#include "fs/fs.h"
#include "syscall/syscall.h"

static fat_read_sector_t g_rs;
static uint32_t g_spc;
static uint32_t g_rsvd;
static uint32_t g_data_start;
static uint32_t g_root_clus;
static uint32_t g_total_sec;
static uint8_t g_sec[512];

#define FAT_EOF 0x0FFFFFF8u
#define FAT_CHAIN_MAX 4096u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_lba(uint32_t lba, uint8_t *dst) {
    /* 1-sector cache: dir scans re-read the same LBA per entry, and
     * every PIO sector costs hundreds of VM exits. Hit rate ~100%. */
    static uint8_t cache[512];
    static uint32_t cache_lba = 0xFFFFFFFFu;
    uint32_t i;
    if (g_rs == 0 || dst == 0) {
        return -NOVA_EINVAL;
    }
    if (lba >= g_total_sec && g_total_sec != 0) {
        return -NOVA_EIO;
    }
    if (lba == cache_lba) {
        for (i = 0; i < 512u; i++) {
            dst[i] = cache[i];
        }
        return NOVA_ESUCCESS;
    }
    if (g_rs(lba, cache) != 0) {
        return -NOVA_EIO;
    }
    cache_lba = lba;
    for (i = 0; i < 512u; i++) {
        dst[i] = cache[i];
    }
    return NOVA_ESUCCESS;
}

int fat_init(fat_read_sector_t rs) {
    uint32_t rsvd, fats, spf;
    if (rs == 0) {
        return -NOVA_EINVAL;
    }
    g_rs = rs;
    g_total_sec = 0; /* unknown until BPB parses: allow LBA0 read */
    if (rs(0, g_sec) != 0) {
        return -NOVA_EIO;
    }
    if (g_sec[510] != 0x55u || g_sec[511] != 0xAAu) {
        return -NOVA_EIO;
    }
    if (rd16(g_sec + 11) != 512) {
        return -NOVA_EIO;
    }
    if (g_sec[13] == 0 || g_sec[16] == 0) {
        return -NOVA_EIO;
    }
    if (g_sec[82] != 'F' || g_sec[83] != 'A' || g_sec[84] != 'T') {
        return -NOVA_EIO;
    }
    g_spc = g_sec[13];
    rsvd = rd16(g_sec + 14);
    fats = g_sec[16];
    spf = rd32(g_sec + 36);
    if (spf == 0 || rsvd == 0) {
        return -NOVA_EIO;
    }
    g_total_sec = rd32(g_sec + 32);
    g_rsvd = rsvd;
    g_data_start = rsvd + fats * spf;
    g_root_clus = rd32(g_sec + 44);
    if (g_root_clus < 2 || g_data_start >= g_total_sec) {
        return -NOVA_EIO;
    }
    return NOVA_ESUCCESS;
}

static uint32_t clus_lba(uint32_t clus) {
    return g_data_start + (clus - 2u) * g_spc;
}

/* FAT entry for a cluster (chain step). */
static int fat_next(uint32_t clus, uint32_t *out) {
    uint32_t off = clus * 4u;
    uint32_t lba;
    if (out == 0 || clus < 2) {
        return -NOVA_EINVAL;
    }
    lba = g_rsvd + off / 512u;
    if (read_lba(lba, g_sec) != 0) {
        return -NOVA_EIO;
    }
    *out = rd32(g_sec + (off % 512u)) & 0x0FFFFFFFu;
    return NOVA_ESUCCESS;
}

/* Uppercase + split "NAME.EXT" into space-padded 8.3 (11B + NUL flag).
 * Returns 0, or -ENOENT when the shape is illegal (multi-dot, empty,
 * overlong parts). Dot and dotdot pass through as ("...") markers:
 * the caller handles them before matching. */
static int name_83(const char *s, uint32_t len, uint8_t out[11]) {
    uint32_t ni = 0, ei = 0, i = 0;
    int dot = 0;
    for (i = 0; i < 11u; i++) {
        out[i] = ' ';
    }
    if (len == 0 || len > 12u) {
        return -NOVA_ENOENT;
    }
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 32);
        }
        if (c == '.') {
            if (dot) {
                return -NOVA_ENOENT;
            }
            dot = 1;
            continue;
        }
        if (c < 32 || c > 126) {
            return -NOVA_ENOENT;
        }
        if (!dot) {
            if (ni >= 8u) {
                return -NOVA_ENOENT;
            }
            out[ni++] = (uint8_t)c;
        } else {
            if (ei >= 3u) {
                return -NOVA_ENOENT;
            }
            out[8u + ei++] = (uint8_t)c;
        }
    }
    if (ni == 0) {
        return -NOVA_ENOENT;
    }
    return NOVA_ESUCCESS;
}

static int name_eq(const uint8_t *entry, const uint8_t want[11]) {
    for (uint32_t i = 0; i < 11u; i++) {
        if (entry[i] != want[i]) {
            return 0;
        }
    }
    return 1;
}

/* Resolve a dir path to its start cluster ("" or "/" = root).
 * Handles dot/dotdot via an explicit cluster stack (no recursion). */
static int dir_clus(const char *path, uint32_t *out) {
    uint32_t stack[FS_MAX_DEPTH + 1u];
    uint32_t depth = 0;
    const char *p;
    if (path == 0 || out == 0) {
        return -NOVA_EINVAL;
    }
    stack[0] = g_root_clus;
    p = path;
    while (*p == '/') {
        p++;
    }
    if (*p == 0) {
        *out = g_root_clus;
        return NOVA_ESUCCESS;
    }
    for (;;) {
        const char *s = p;
        uint32_t len = 0;
        uint8_t want[11];
        uint32_t ent;
        int rc;
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
            if (depth >= FS_MAX_DEPTH) {
                return -NOVA_ENAMETOOLONG;
            }
            if (name_83(s, len, want) != 0) {
                return -NOVA_ENOENT;
            }
            rc = -NOVA_ENOENT;
            /* find subdir `want` inside stack[depth] */
            {
                uint32_t clus = stack[depth];
                uint32_t steps = 0;
                int saw_file = 0;
                while (clus < FAT_EOF && steps++ < FAT_CHAIN_MAX) {
                    uint32_t per = g_spc * 16u;
                    for (ent = 0; ent < per; ent++) {
                        uint32_t lba =
                            clus_lba(clus) + (ent * 32u) / 512u;
                        uint8_t first;
                        const uint8_t *e;
                        if (read_lba(lba, g_sec) != 0) {
                            return -NOVA_EIO;
                        }
                        e = g_sec + (ent * 32u) % 512u;
                        first = e[0];
                        if (first == 0x00) {
                            /* End of directory: entries never
                             * continue past it (not per-cluster). */
                            return saw_file ? -NOVA_ENOTDIR
                                            : -NOVA_ENOENT;
                        }
                        if (first == 0xE5 || e[11] == 0x0F ||
                            (e[11] & 0x08u) != 0) {
                            continue;
                        }
                        if (name_eq(e, want)) {
                            if ((e[11] & 0x10u) == 0) {
                                saw_file = 1;
                                continue;
                            }
                            uint32_t nc =
                                ((uint32_t)rd16(e + 20) << 16) |
                                rd16(e + 26);
                            if (nc < 2) {
                                return -NOVA_EIO;
                            }
                            stack[++depth] = nc;
                            rc = NOVA_ESUCCESS;
                            ent = per;
                            break;
                        }
                    }
                    if (rc == 0) {
                        break;
                    }
                    if (fat_next(clus, &clus) != 0) {
                        return -NOVA_EIO;
                    }
                }
                if (rc != 0 && saw_file) {
                    /* Component exists but is a file, not a dir. */
                    return -NOVA_ENOTDIR;
                }
            }
            if (rc != 0) {
                return rc;
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

/* Find an entry by 8.3 name inside a dir cluster: cluster + attr +
 * size. Skips deleted/LFN/volume slots; 0x00 ends the scan. */
static int dir_find(uint32_t dclus, const uint8_t want[11],
                    uint32_t *clus_out, uint8_t *attr_out,
                    uint32_t *size_out) {
    uint32_t clus = dclus;
    uint32_t steps = 0;
    if (clus < 2) {
        return -NOVA_EINVAL;
    }
    while (clus < FAT_EOF && steps++ < FAT_CHAIN_MAX) {
        uint32_t per = g_spc * 16u;
        for (uint32_t ent = 0; ent < per; ent++) {
            uint32_t lba = clus_lba(clus) + (ent * 32u) / 512u;
            const uint8_t *e;
            if (read_lba(lba, g_sec) != 0) {
                return -NOVA_EIO;
            }
            e = g_sec + (ent * 32u) % 512u;
            if (e[0] == 0x00) {
                return -NOVA_ENOENT;
            }
            if (e[0] == 0xE5 || e[11] == 0x0F ||
                (e[11] & 0x08u) != 0) {
                continue;
            }
            if (name_eq(e, want)) {
                uint32_t nc = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
                if (attr_out != 0) {
                    *attr_out = e[11];
                }
                if (size_out != 0) {
                    *size_out = rd32(e + 28);
                }
                if (clus_out != 0) {
                    *clus_out = nc;
                }
                return NOVA_ESUCCESS;
            }
        }
        if (fat_next(clus, &clus) != 0) {
            return -NOVA_EIO;
        }
    }
    return -NOVA_ENOENT;
}

/* Split "/a/b/C" into dir part (cluster) + final 8.3 name. */
static int split_parent(const char *path, uint32_t *dclus_out,
                        uint8_t want[11]) {
    const char *last = 0;
    const char *p = path;
    char dir[FS_MAX_PATH + 1u];
    uint32_t dl;
    while (*p == '/') {
        p++;
    }
    if (*p == 0) {
        return -NOVA_EINVAL;
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
    /* dir prefix = everything before `last` (root when empty). */
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
    {
        int drc = dir_clus(dir, dclus_out);
        if (drc != 0) {
            /* Preserve ENOTDIR (through-a-file) vs ENOENT. */
            return drc;
        }
    }
    {
        uint32_t len = 0;
        while (last[len] != 0 && last[len] != '/') {
            len++;
        }
        return name_83(last, len, want);
    }
}

int fat_stat(const char *path, uint32_t *size_out, int *is_dir_out) {
    uint32_t dclus;
    uint8_t want[11];
    uint32_t clus = 0;
    uint8_t attr = 0;
    uint32_t size = 0;
    int rc;
    if (path == 0) {
        return -NOVA_EINVAL;
    }
    /* Bare dir path (no final file component): stat the dir itself. */
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
        /* Trailing slash forces dir interpretation. */
        {
            uint32_t L = 0;
            while (p[L] != 0) {
                L++;
            }
            if (L > 0 && p[L - 1u] == '/') {
                if (dir_clus(path, &dclus) != 0) {
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
    rc = split_parent(path, &dclus, want);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dclus, want, &clus, &attr, &size);
    if (rc != 0) {
        /* Maybe it names a dir (no trailing slash): resolve fully. */
        if (dir_clus(path, &dclus) == 0) {
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
        *size_out = size;
    }
    if (is_dir_out != 0) {
        *is_dir_out = (attr & 0x10u) != 0;
    }
    return NOVA_ESUCCESS;
}

int fat_read_file(const char *path, uint8_t *dst, uint32_t max,
                  uint32_t *len_out) {
    uint32_t dclus;
    uint8_t want[11];
    uint32_t clus = 0;
    uint8_t attr = 0;
    uint32_t size = 0;
    uint32_t got = 0;
    uint32_t steps = 0;
    int rc;
    if (path == 0 || dst == 0 || len_out == 0) {
        return -NOVA_EINVAL;
    }
    rc = split_parent(path, &dclus, want);
    if (rc != 0) {
        return rc;
    }
    rc = dir_find(dclus, want, &clus, &attr, &size);
    if (rc != 0) {
        return rc;
    }
    if ((attr & 0x10u) != 0) {
        return -NOVA_EISDIR;
    }
    if (size > max) {
        return -NOVA_ENOSPC;
    }
    if (size == 0) {
        *len_out = 0;
        return NOVA_ESUCCESS;
    }
    if (clus < 2) {
        return -NOVA_EIO;
    }
    while (got < size && steps++ < FAT_CHAIN_MAX) {
        uint32_t lba = clus_lba(clus);
        for (uint32_t s = 0; s < g_spc && got < size; s++) {
            uint32_t n = size - got;
            uint32_t i;
            if (n > 512u) {
                n = 512u;
            }
            if (read_lba(lba + s, g_sec) != 0) {
                return -NOVA_EIO;
            }
            for (i = 0; i < n; i++) {
                dst[got + i] = g_sec[i];
            }
            got += n;
        }
        if (got >= size) {
            break;
        }
        if (fat_next(clus, &clus) != 0) {
            return -NOVA_EIO;
        }
        if (clus >= FAT_EOF) {
            return -NOVA_EIO;
        }
    }
    if (got != size) {
        return -NOVA_EIO;
    }
    *len_out = got;
    return NOVA_ESUCCESS;
}

int fat_list_dir(const char *path, uint32_t index, char *name_out) {
    uint32_t dclus;
    uint32_t clus;
    uint32_t steps = 0;
    uint32_t seen = 0;
    int rc = dir_clus(path, &dclus);
    if (rc != 0) {
        return rc;
    }
    clus = dclus;
    while (clus < FAT_EOF && steps++ < FAT_CHAIN_MAX) {
        uint32_t per = g_spc * 16u;
        for (uint32_t ent = 0; ent < per; ent++) {
            uint32_t lba = clus_lba(clus) + (ent * 32u) / 512u;
            const uint8_t *e;
            uint32_t k;
            uint32_t ni = 0, ei = 0;
            if (read_lba(lba, g_sec) != 0) {
                return -NOVA_EIO;
            }
            e = g_sec + (ent * 32u) % 512u;
            if (e[0] == 0x00) {
                return -NOVA_ENOENT;
            }
            if (e[0] == 0xE5 || e[11] == 0x0F ||
                (e[11] & 0x08u) != 0) {
                continue;
            }
            /* Hide dot entries (cleaner `ls`; reachable by path). */
            if (e[0] == '.' &&
                (e[1] == ' ' || (e[1] == '.' && e[2] == ' '))) {
                continue;
            }
            if (seen != index) {
                seen++;
                continue;
            }
            /* 8.3 -> "NAME.EXT" (spaces stripped, dot iff ext). */
            while (ni < 8u && e[ni] != ' ') {
                name_out[ni] = (char)e[ni];
                ni++;
            }
            while (ei < 3u && e[8u + ei] != ' ') {
                ei++;
            }
            k = ni;
            if (ei > 0) {
                name_out[k++] = '.';
                for (uint32_t j = 0; j < ei; j++) {
                    name_out[k++] = (char)e[8u + j];
                }
            }
            name_out[k] = 0;
            return (int)k;
        }
        if (fat_next(clus, &clus) != 0) {
            return -NOVA_EIO;
        }
    }
    return -NOVA_ENOENT;
}

static int fat_eq(const uint8_t *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != (uint8_t)b[i]) {
            return 0;
        }
    }
    return 1;
}

int fat_selftest(void) {
    /* 512B cap: bigger frames trip __chkstk_ms (no libgcc on link). */
    static uint8_t buf[512];
    uint32_t size = 0;
    int is_dir = 0;
    uint32_t got = 0;
    char name[FS_MAX_NAME];
    /* CONTRACT with tools/mkfat.py (asserted, not eyeballed). */
    if (fat_stat("/HELLO.TXT", &size, &is_dir) != 0 || size != 17u ||
        is_dir) {
        serial_puts("FAT-FAIL hello-stat\n");
        return -1;
    }
    if (fat_read_file("/HELLO.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 17u || !fat_eq(buf, "hello from fat32\n", 17u)) {
        serial_puts("FAT-FAIL hello-bytes\n");
        return -1;
    }
    /* Lowercase input uppercases on match. */
    if (fat_read_file("/hello.txt", buf, sizeof(buf), &got) != 0 ||
        got != 17u) {
        serial_puts("FAT-FAIL lower\n");
        return -1;
    }
    if (fat_stat("/DOCS", &size, &is_dir) != 0 || !is_dir) {
        serial_puts("FAT-FAIL docs-stat\n");
        return -1;
    }
    if (fat_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) != 0 ||
        got != 16u || !fat_eq(buf, "fat32 nested ok\n", 16u)) {
        serial_puts("FAT-FAIL note-bytes\n");
        return -1;
    }
    /* Root listing: exactly HELLO.TXT + DOCS (order = disk order). */
    if (fat_list_dir("/", 0, name) < 0) {
        serial_puts("FAT-FAIL ls0\n");
        return -1;
    }
    {
        int hello = 0, docs = 0;
        for (uint32_t i = 0; i < 8u; i++) {
            int rc = fat_list_dir("/", i, name);
            if (rc == -NOVA_ENOENT) {
                break;
            }
            if (rc < 0) {
                serial_puts("FAT-FAIL ls-err\n");
                return -1;
            }
            if (fat_eq((const uint8_t *)name, "HELLO.TXT", 9u)) {
                hello = 1;
            }
            if (fat_eq((const uint8_t *)name, "DOCS", 4u)) {
                docs = 1;
            }
        }
        if (!hello || !docs) {
            serial_puts("FAT-FAIL ls-names\n");
            return -1;
        }
    }
    if (fat_list_dir("/DOCS", 9, name) != -NOVA_ENOENT) {
        serial_puts("FAT-FAIL ls-end\n");
        return -1;
    }
    if (fat_stat("/NOPE.TXT", &size, &is_dir) != -NOVA_ENOENT) {
        serial_puts("FAT-FAIL enoent\n");
        return -1;
    }
    if (fat_read_file("/DOCS", buf, sizeof(buf), &got) != -NOVA_EISDIR) {
        serial_puts("FAT-FAIL eisdir\n");
        return -1;
    }
    serial_puts("FAT-OK\n");
    return NOVA_ESUCCESS;
}
