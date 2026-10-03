/* NOVA OS FAT32 host test (Phase 7c, T10).
 * Host-side: the REAL kernel/fs/fat32.c against the REAL generated
 * image (tools/mkfat.py output, path from argv[1]). Contract checks
 * (BPB geometry, known bytes, nesting, case-folding, dot paths,
 * error discipline) plus a randomized 8.3-name fuzz for
 * crash-freedom. VERDICT line on success (mirrors T7-T9 style).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "fs/fat32.h"
#include "fs/fs.h"
/* mingw stdio.h already defines SYS_OPEN: drop it before our ABI. */
#undef SYS_OPEN
#include "syscall/syscall.h"

static int g_failed;

static void check(int cond, const char *what, unsigned long a,
                  unsigned long b) {
    if (!cond) {
        printf("FAIL: %s (%lu, %lu)\n", what, a, b);
        g_failed = 1;
    }
}

/* --- kernel dep stubs --- */

__attribute__((noreturn)) void nova_panic(const char *reason) {
    printf("PANIC: %s\n", reason);
    exit(2);
}

void serial_puts(const char *s) {
    fputs(s, stdout);
}

void serial_putc(char c) {
    putchar(c);
}

void serial_putdec32(uint32_t v) {
    printf("%u", v);
}

void serial_puthex32(uint32_t v) {
    printf("%08x", v);
}

/* --- image-backed sector source --- */

static uint8_t *g_img;
static size_t g_len;

static int img_read(uint32_t lba, uint8_t *dst) {
    size_t off = (size_t)lba * 512u;
    if (off + 512u > g_len) {
        return -NOVA_EIO;
    }
    memcpy(dst, g_img + off, 512u);
    return 0;
}

static int img_write(uint32_t lba, const uint8_t *src) {
    size_t off = (size_t)lba * 512u;
    if (off + 512u > g_len) {
        return -NOVA_EIO;
    }
    memcpy(g_img + off, src, 512u);
    return 0;
}

static uint32_t lcg_state = 0x76543210u;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(int argc, char **argv) {
    uint8_t buf[512];
    uint32_t size = 0;
    int is_dir = 0;
    uint32_t got = 0;
    char name[FS_MAX_NAME];
    FILE *f;
    const char *path = (argc > 1) ? argv[1] : "build/fat32.raw";

    f = fopen(path, "rb");
    if (f == 0) {
        printf("FAIL: cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    {
        long n = ftell(f);
        if (n <= 0) {
            printf("FAIL: empty image\n");
            fclose(f);
            return 1;
        }
        g_len = (size_t)n;
    }
    fseek(f, 0, SEEK_SET);
    g_img = (uint8_t *)malloc(g_len);
    if (g_img == 0 || fread(g_img, 1, g_len, f) != g_len) {
        printf("FAIL: image read\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    check(fat_init(img_read, img_write) == 0, "init", 0, 0);

    /* Contract with tools/mkfat.py. */
    check(fat_stat("/HELLO.TXT", &size, &is_dir) == 0 && size == 17u &&
              !is_dir,
          "hello-stat", size, (unsigned long)is_dir);
    check(fat_read_file("/HELLO.TXT", buf, sizeof(buf), &got) == 0 &&
              got == 17u && memcmp(buf, "hello from fat32\n", 17u) == 0,
          "hello-bytes", got, 0);
    check(fat_stat("/DOCS/NOTE.TXT", &size, &is_dir) == 0 && size == 16u &&
              !is_dir,
          "note-stat", size, 0);
    check(fat_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) == 0 &&
              got == 16u && memcmp(buf, "fat32 nested ok\n", 16u) == 0,
          "note-bytes", got, 0);

    /* Case folding + dot paths + trailing slashes. */
    check(fat_stat("/docs/note.txt", &size, &is_dir) == 0 && size == 16u,
          "lower", size, 0);
    check(fat_stat("/DOCS/../HELLO.TXT", &size, &is_dir) == 0 &&
              size == 17u,
          "dotdot", size, 0);
    check(fat_stat("//DOCS//", &size, &is_dir) == 0 && is_dir,
          "slashes", size, (unsigned long)is_dir);
    check(fat_stat("/", &size, &is_dir) == 0 && is_dir, "root", 0, 0);

    /* Bounds: tiny buffer refuses big file; exact read works. */
    check(fat_read_file("/HELLO.TXT", buf, 5, &got) == -NOVA_ENOSPC,
          "small-buf", 0, 0);
    check(fat_read_file("/HELLO.TXT", buf, 17, &got) == 0 && got == 17u,
          "exact-buf", got, 0);

    /* Errors, not crashes. */
    check(fat_stat("/NOPE.TXT", &size, &is_dir) == -NOVA_ENOENT, "enoent",
          0, 0);
    check(fat_stat("/HELLO.TXT/X", &size, &is_dir) == -NOVA_ENOTDIR,
          "enotdir", 0, 0);
    check(fat_read_file("/DOCS", buf, sizeof(buf), &got) == -NOVA_EISDIR,
          "eisdir", 0, 0);
    check(fat_list_dir("/NOPE", 0, name) == -NOVA_ENOENT, "ls-enoent", 0,
          0);

    /* Listing shape: root has exactly 2 visible entries. */
    {
        uint32_t n = 0;
        for (;;) {
            int rc = fat_list_dir("/", n, name);
            if (rc == -NOVA_ENOENT) {
                break;
            }
            check(rc > 0, "ls-len", (unsigned long)rc, n);
            n++;
            if (n > 16u) {
                break;
            }
        }
        check(n == 2, "ls-count", n, 0);
    }

    /* Write path on the image copy (mirrors + extends the guest
     * hermetic round-trip): multi-cluster pattern with full read-back,
     * mkdir + nested file, ENOSPC honesty, delete hygiene. */
    {
        static uint8_t pat[4096];
        static uint8_t back[4096];
        uint32_t g3 = 0;
        for (uint32_t i = 0; i < sizeof(pat); i++) {
            pat[i] = (uint8_t)((i * 13u + 7u) & 0xFFu);
        }
        check(fat_create_file("/W.TXT") == 0, "w-create", 0, 0);
        check(fat_create_file("/W.TXT") == -NOVA_EEXIST, "w-exist", 0,
              0);
        check(fat_write_at("/W.TXT", 0, pat, 3000u) == 0, "w-3000", 0,
              0);
        check(fat_stat("/W.TXT", &size, &is_dir) == 0 && size == 3000u &&
                  !is_dir,
              "w-size", size, 0);
        check(fat_read_file("/W.TXT", back, sizeof(back), &g3) == 0 &&
                  g3 == 3000u && memcmp(back, pat, 3000u) == 0,
              "w-bytes", g3, 0);
        /* Offset overwrite inside the chain (partial-sector merge). */
        check(fat_write_at("/W.TXT", 1000u, pat, 100u) == 0, "w-over",
              0, 0);
        check(fat_read_file("/W.TXT", back, sizeof(back), &g3) == 0 &&
                  g3 == 3000u && memcmp(back + 1000u, pat, 100u) == 0 &&
                  back[999] == (uint8_t)((999u * 13u + 7u) & 0xFFu),
              "w-merge", g3, 0);
        check(fat_mkdir("/NDIR") == 0, "w-mkdir", 0, 0);
        check(fat_mkdir("/NDIR") == -NOVA_EEXIST, "w-mkdir-exist", 0,
              0);
        check(fat_create_file("/NDIR/F.TXT") == 0, "w-nested", 0, 0);
        check(fat_write_at("/NDIR/F.TXT", 0, pat, 16u) == 0, "w-nw",
              0, 0);
        check(fat_list_dir("/NDIR", 0, name) == 5 &&
                  strcmp(name, "F.TXT") == 0,
              "w-nls", 0, 0);
        check(fat_list_dir("/NDIR", 1, name) == -NOVA_ENOENT, "w-nend",
              0, 0);
        /* ENOSPC: 64KB needs ~128 clusters, disk holds ~26 free. */
        {
            static uint8_t *big = 0;
            int wr;
            if (big == 0) {
                big = (uint8_t *)malloc(FS_MAX_FILE);
            }
            check(big != 0, "w-bigalloc", 0, 0);
            if (big != 0) {
                memset(big, 0xAA, FS_MAX_FILE);
                check(fat_create_file("/HUGE.BIN") == 0, "w-huge-c",
                      0, 0);
                wr = fat_write_at("/HUGE.BIN", 0, big, FS_MAX_FILE);
                check(wr == -NOVA_ENOSPC, "w-enospc",
                      (unsigned long)wr, 0);
                check(fat_delete("/HUGE.BIN") == 0, "w-huge-del", 0,
                      0);
            }
        }
        /* Fixture files survived the mutation storm intact. */
        check(fat_read_file("/HELLO.TXT", buf, sizeof(buf), &got) == 0 &&
                  got == 17u,
              "w-fixture", got, 0);
        check(fat_delete("/W.TXT") == 0, "w-del", 0, 0);
        check(fat_delete("/NDIR/F.TXT") == 0, "w-deln", 0, 0);
        check(fat_stat("/W.TXT", &size, &is_dir) == -NOVA_ENOENT,
              "w-gone", 0, 0);
        check(fat_delete("/W.TXT") == -NOVA_ENOENT, "w-del2", 0, 0);
        check(fat_delete("/NDIR") == -NOVA_EISDIR, "w-deldir", 0, 0);
    }

    /* Randomized 8.3-ish fuzz (crash-freedom + errno discipline). */
    {
        static const char alpha[] = "ABCHLMNOPSTUX.-_012 ";
        for (int t = 0; t < 20000; t++) {
            char p[40];
            uint32_t at = 0;
            int ncomp = 1 + (int)(lcg() % 3u);
            uint32_t g2 = 0;
            int d2 = 0;
            uint8_t sb[32];
            uint32_t gg = 0;
            int r2;
            p[at++] = '/';
            for (int k = 0; k < ncomp; k++) {
                int L = (int)(lcg() % 10u);
                if (at + (uint32_t)L + 1 >= sizeof(p)) {
                    break;
                }
                for (int j = 0; j < L; j++) {
                    p[at++] =
                        alpha[lcg() % (sizeof(alpha) - 1u)];
                }
                p[at++] = '/';
            }
            p[at] = 0;
            r2 = fat_stat(p, &g2, &d2);
            check(r2 == 0 || r2 == -NOVA_ENOENT || r2 == -NOVA_EINVAL ||
                      r2 == -NOVA_ENOTDIR || r2 == -NOVA_EIO,
                  "fuzz-stat", (unsigned long)r2, (unsigned long)t);
            if (r2 == 0 && !d2) {
                int r3 = fat_read_file(p, sb, sizeof(sb), &gg);
                check(r3 == 0 || r3 == -NOVA_ENOSPC,
                      "fuzz-read", (unsigned long)r3, 0);
            }
            {
                int r4 = fat_list_dir(p, lcg() % 5u, name);
                check(r4 >= 0 || r4 == -NOVA_ENOENT ||
                          r4 == -NOVA_EINVAL || r4 == -NOVA_EIO,
                      "fuzz-ls", (unsigned long)r4, 0);
            }
        }
        printf("fuzz: 20000 randomized fat paths checked\n");
    }

    /* Guest selftest runs the same stack in-guest (FAT-OK). */
    check(fat_selftest() == 0, "selftest", 0, 0);

    free(g_img);
    if (g_failed) {
        printf("VERDICT: fat32 BROKEN\n");
        return 1;
    }
    printf("VERDICT: fat32 correct\n");
    return 0;
}
