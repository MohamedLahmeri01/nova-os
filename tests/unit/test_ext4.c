/* NOVA OS EXT4 host test (Phase 7e, T11).
 * Host-side: the REAL kernel/fs/ext4.c against the REAL generated
 * image (tools/mkext4.py output, path from argv[1]). Contract checks
 * (superblock geometry, known bytes, nesting, case-sensitivity, dot
 * paths, error discipline) plus a randomized name fuzz for
 * crash-freedom. VERDICT line on success (mirrors T7-T10 style).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "fs/ext4.h"
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

static uint32_t lcg_state = 0xE4E4E4E4u;

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
    const char *path = (argc > 1) ? argv[1] : "build/ext4.raw";

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

    check(ext_init(img_read) == 0, "init", 0, 0);

    /* Contract with tools/mkext4.py (both files are 16B). */
    check(ext_stat("/HELLO.TXT", &size, &is_dir) == 0 && size == 16u &&
              !is_dir,
          "hello-stat", size, (unsigned long)is_dir);
    check(ext_read_file("/HELLO.TXT", buf, sizeof(buf), &got) == 0 &&
              got == 16u && memcmp(buf, "hello from ext4\n", 16u) == 0,
          "hello-bytes", got, 0);
    check(ext_stat("/DOCS/NOTE.TXT", &size, &is_dir) == 0 &&
              size == 15u && !is_dir,
          "note-stat", size, 0);
    check(ext_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) == 0 &&
              got == 15u && memcmp(buf, "ext4 nested ok\n", 15u) == 0,
          "note-bytes", got, 0);

    /* Case-sensitive (extra vs FAT): lowercase misses. */
    check(ext_stat("/hello.txt", &size, &is_dir) == -NOVA_ENOENT,
          "case", 0, 0);

    /* Dot paths + trailing slashes. */
    check(ext_stat("/DOCS/../HELLO.TXT", &size, &is_dir) == 0 &&
              size == 16u,
          "dotdot", size, 0);
    check(ext_stat("//DOCS//", &size, &is_dir) == 0 && is_dir,
          "slashes", size, (unsigned long)is_dir);
    check(ext_stat("/", &size, &is_dir) == 0 && is_dir, "root", 0, 0);

    /* Bounds + errors. */
    check(ext_read_file("/HELLO.TXT", buf, 5, &got) == -NOVA_ENOSPC,
          "small-buf", 0, 0);
    check(ext_stat("/NOPE", &size, &is_dir) == -NOVA_ENOENT, "enoent",
          0, 0);
    check(ext_stat("/HELLO.TXT/X", &size, &is_dir) == -NOVA_ENOTDIR,
          "enotdir", 0, 0);
    check(ext_read_file("/DOCS", buf, sizeof(buf), &got) ==
              -NOVA_EISDIR,
          "eisdir", 0, 0);
    check(ext_list_dir("/NOPE", 0, name) == -NOVA_ENOENT, "ls-enoent",
          0, 0);

    /* Listing shape: root has exactly 2 visible entries. */
    {
        uint32_t n = 0;
        for (;;) {
            int rc = ext_list_dir("/", n, name);
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

    /* Randomized name fuzz (crash-freedom + errno discipline). */
    {
        static const char alpha[] = "ABCHLMNOSTUX._012abc";
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
            r2 = ext_stat(p, &g2, &d2);
            check(r2 == 0 || r2 == -NOVA_ENOENT || r2 == -NOVA_EINVAL ||
                      r2 == -NOVA_ENOTDIR || r2 == -NOVA_EIO,
                  "fuzz-stat", (unsigned long)r2, (unsigned long)t);
            if (r2 == 0 && !d2) {
                int r3 = ext_read_file(p, sb, sizeof(sb), &gg);
                check(r3 == 0 || r3 == -NOVA_ENOSPC,
                      "fuzz-read", (unsigned long)r3, 0);
            }
            {
                int r4 = ext_list_dir(p, lcg() % 5u, name);
                check(r4 >= 0 || r4 == -NOVA_ENOENT ||
                          r4 == -NOVA_EINVAL || r4 == -NOVA_EIO,
                      "fuzz-ls", (unsigned long)r4, 0);
            }
        }
        printf("fuzz: 20000 randomized ext paths checked\n");
    }

    /* Guest selftest runs the same stack in-guest (EXT-OK). */
    check(ext_selftest() == 0, "selftest", 0, 0);

    free(g_img);
    if (g_failed) {
        printf("VERDICT: ext4 BROKEN\n");
        return 1;
    }
    printf("VERDICT: ext4 correct\n");
    return 0;
}
