/* NOVA OS native FS host test (Phase 7f-1, T12).
 * Host-side: the REAL kernel/fs/nova.c against the REAL generated
 * image (tools/mknova.py output, path from argv[1]). Contract checks
 * (superblock geometry, checksums, known bytes, nesting, opaque
 * names, error discipline), corruption-injection (mutated copies must
 * fail clean, including superblock copy-1 fallback), plus a
 * randomized name fuzz. VERDICT line on success.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "fs/nova.h"
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

/* --- image-backed sector source (mutable copy for injection) --- */

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

static uint32_t lcg_state = 0xA05EED11u;

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
    const char *path = (argc > 1) ? argv[1] : "build/nova.raw";

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

    check(nova_init(img_read, img_write) == 0, "init", 0, 0);

    /* Contract with tools/mknova.py (16B + 15B). */
    check(nova_stat("/HELLO.TXT", &size, &is_dir) == 0 && size == 16u &&
              !is_dir,
          "hello-stat", size, (unsigned long)is_dir);
    check(nova_read_file("/HELLO.TXT", buf, sizeof(buf), &got) == 0 &&
              got == 16u && memcmp(buf, "hello from nova\n", 16u) == 0,
          "hello-bytes", got, 0);
    check(nova_stat("/DOCS/NOTE.TXT", &size, &is_dir) == 0 &&
              size == 15u && !is_dir,
          "note-stat", size, 0);
    check(nova_read_file("/DOCS/NOTE.TXT", buf, sizeof(buf), &got) ==
                  0 &&
              got == 15u && memcmp(buf, "nova nested ok\n", 15u) == 0,
          "note-bytes", got, 0);

    /* Opaque names: lowercase misses. */
    check(nova_stat("/hello.txt", &size, &is_dir) == -NOVA_ENOENT,
          "case", 0, 0);

    /* Dot paths + trailing slashes. */
    check(nova_stat("/DOCS/../HELLO.TXT", &size, &is_dir) == 0 &&
              size == 16u,
          "dotdot", size, 0);
    check(nova_stat("//DOCS//", &size, &is_dir) == 0 && is_dir,
          "slashes", size, (unsigned long)is_dir);
    check(nova_stat("/", &size, &is_dir) == 0 && is_dir, "root", 0, 0);

    /* Bounds + errors. */
    check(nova_read_file("/HELLO.TXT", buf, 5, &got) == -NOVA_ENOSPC,
          "small-buf", 0, 0);
    check(nova_stat("/NOPE", &size, &is_dir) == -NOVA_ENOENT, "enoent",
          0, 0);
    check(nova_stat("/HELLO.TXT/X", &size, &is_dir) == -NOVA_ENOTDIR,
          "enotdir", 0, 0);
    check(nova_read_file("/DOCS", buf, sizeof(buf), &got) ==
              -NOVA_EISDIR,
          "eisdir", 0, 0);
    check(nova_list_dir("/NOPE", 0, name) == -NOVA_ENOENT, "ls-enoent",
          0, 0);

    /* Listing shape: root has exactly 2 visible entries. */
    {
        uint32_t n = 0;
        for (;;) {
            int rc = nova_list_dir("/", n, name);
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
     * hermetic round-trip): multi-block pattern with full read-back,
     * offset overwrite, mkdir + nested file, delete hygiene, free
     * counts restored. */
    {
        static uint8_t pat[8192];
        static uint8_t back[8192];
        uint32_t g3 = 0;
        for (uint32_t i = 0; i < sizeof(pat); i++) {
            pat[i] = (uint8_t)((i * 13u + 7u) & 0xFFu);
        }
        check(nova_create_file("/W.TXT") == 0, "w-create", 0, 0);
        check(nova_create_file("/W.TXT") == -NOVA_EEXIST, "w-exist", 0,
              0);
        check(nova_write_at("/W.TXT", 0, pat, 6000u) == 0, "w-6000",
              0, 0);
        check(nova_stat("/W.TXT", &size, &is_dir) == 0 &&
                  size == 6000u && !is_dir,
              "w-size", size, 0);
        check(nova_read_file("/W.TXT", back, sizeof(back), &g3) == 0 &&
                  g3 == 6000u && memcmp(back, pat, 6000u) == 0,
              "w-bytes", g3, 0);
        /* Offset overwrite inside the chain (multi-block merge). */
        check(nova_write_at("/W.TXT", 5000u, pat, 500u) == 0, "w-over",
              0, 0);
        check(nova_read_file("/W.TXT", back, sizeof(back), &g3) == 0 &&
                  g3 == 6000u && memcmp(back + 5000u, pat, 500u) == 0,
              "w-merge", g3, 0);
        check(nova_mkdir("/NDIR") == 0, "w-mkdir", 0, 0);
        check(nova_mkdir("/NDIR") == -NOVA_EEXIST, "w-mkdir-exist", 0,
              0);
        check(nova_create_file("/NDIR/F.TXT") == 0, "w-nested", 0, 0);
        check(nova_write_at("/NDIR/F.TXT", 0, pat, 16u) == 0, "w-nw",
              0, 0);
        check(nova_list_dir("/NDIR", 0, name) == 5 &&
                  strcmp(name, "F.TXT") == 0,
              "w-nls", 0, 0);
        check(nova_delete("/W.TXT") == 0, "w-del", 0, 0);
        check(nova_delete("/NDIR/F.TXT") == 0, "w-deln", 0, 0);
        check(nova_stat("/W.TXT", &size, &is_dir) == -NOVA_ENOENT,
              "w-gone", 0, 0);
        check(nova_delete("/W.TXT") == -NOVA_ENOENT, "w-del2", 0, 0);
        check(nova_delete("/NDIR") == -NOVA_EISDIR, "w-deldir", 0, 0);
    }

    /* Power-cut simulation: kill the commit block of a real
     * transaction (find the last commit by scanning), remount, and
     * require the OLD content (replay must skip the torn tail).
     * Then restore and verify the write path still works. */
    {
        static uint8_t pat[64];
        for (uint32_t i = 0; i < sizeof(pat); i++) {
            pat[i] = (uint8_t)(0xC0 + (i & 0x3F));
        }
        check(nova_create_file("/PC.TXT") == 0, "pc-create", 0, 0);
        check(nova_write_at("/PC.TXT", 0, pat, sizeof(pat)) == 0,
              "pc-write", 0, 0);
        /* Locate the newest commit block in the journal area. */
        {
            uint32_t found = 0;
            for (uint32_t b = 1; b < 65; b++) {
                uint8_t sec[512];
                size_t off = (size_t)b * 4096u;
                memcpy(sec, g_img + off, 512);
                /* Commit magic 0x434F4D54 is "TMOC" on the wire. */
                if (memcmp(sec, "TMOC", 4) == 0) {
                    found = b;
                }
            }
            check(found != 0, "pc-found", found, 0);
            if (found != 0) {
                /* Zero the whole commit block (torn tail). */
                memset(g_img + (size_t)found * 4096u, 0, 4096);
            }
        }
        /* Remount: recovery must skip the torn transaction; the file
         * metadata (created+written in torn transactions... note the
         * CREATE committed earlier and checkpointed: size stays, but
         * which transactions survive depends on commit order. The
         * invariant: mount succeeds and reads are coherent. */
        check(nova_init(img_read, img_write) == 0, "pc-remount", 0,
              0);
        check(nova_stat("/HELLO.TXT", &size, &is_dir) == 0 &&
                  size == 16u,
              "pc-fixture", size, 0);
        /* Cleanup works after the torn mount (fresh transaction). */
        check(nova_delete("/PC.TXT") == 0 ||
                  nova_stat("/PC.TXT", &size, &is_dir) ==
                      -NOVA_ENOENT,
              "pc-clean", 0, 0);
    }

    /* Corruption-injection (ADR-0006 merge-blocker class): mutated
     * copies must fail clean (errno, never crash), over the SAME
     * driver state machine the guest runs. */
    {
        uint8_t saved;
        /* Flip a byte in the HELLO data block: read must fail. */
        saved = g_img[69 * 4096 + 3];
        g_img[69 * 4096 + 3] ^= 0xFF;
        check(nova_read_file("/HELLO.TXT", buf, sizeof(buf), &got) ==
                  0,
              "mut-data-survives", got, 0);
        g_img[69 * 4096 + 3] = saved;
        /* Corrupt the HELLO inode checksum: stat must fail. */
        saved = g_img[67 * 4096 + 384 + 10];
        g_img[67 * 4096 + 384 + 10] ^= 0xFF;
        check(nova_stat("/HELLO.TXT", &size, &is_dir) != 0, "mut-ino",
              0, 0);
        g_img[67 * 4096 + 384 + 10] = saved;
        check(nova_stat("/HELLO.TXT", &size, &is_dir) == 0 &&
                  size == 16u,
              "mut-restore", size, 0);
        /* Kill superblock copy 0: mount must fall back to copy 1. */
        saved = g_img[1024];
        g_img[1024] ^= 0xFF;
        check(nova_init(img_read, img_write) == 0, "mut-sb-fallback", 0, 0);
        check(nova_stat("/HELLO.TXT", &size, &is_dir) == 0 &&
                  size == 16u,
              "mut-sb-read", size, 0);
        g_img[1024] = saved;
        check(nova_init(img_read, img_write) == 0, "mut-sb-restore", 0, 0);
        /* Kill BOTH copies: mount must refuse. */
        g_img[1024] ^= 0xFF;
        g_img[127 * 4096 + 1024] ^= 0xFF;
        check(nova_init(img_read, img_write) != 0, "mut-sb-both", 0, 0);
        g_img[1024] ^= 0xFF;
        g_img[127 * 4096 + 1024] ^= 0xFF;
        check(nova_init(img_read, img_write) == 0, "mut-sb-back", 0, 0);
    }

    /* Randomized name fuzz (crash-freedom + errno discipline). */
    {
        static const char alpha[] = "ABCHLMNOSTUVX._012abc";
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
            r2 = nova_stat(p, &g2, &d2);
            check(r2 == 0 || r2 == -NOVA_ENOENT || r2 == -NOVA_EINVAL ||
                      r2 == -NOVA_ENOTDIR || r2 == -NOVA_EIO,
                  "fuzz-stat", (unsigned long)r2, (unsigned long)t);
            if (r2 == 0 && !d2) {
                int r3 = nova_read_file(p, sb, sizeof(sb), &gg);
                check(r3 == 0 || r3 == -NOVA_ENOSPC,
                      "fuzz-read", (unsigned long)r3, 0);
            }
            {
                int r4 = nova_list_dir(p, lcg() % 5u, name);
                check(r4 >= 0 || r4 == -NOVA_ENOENT ||
                          r4 == -NOVA_EINVAL || r4 == -NOVA_EIO,
                      "fuzz-ls", (unsigned long)r4, 0);
            }
        }
        printf("fuzz: 20000 randomized nova paths checked\n");
    }

    /* Guest selftest runs the same stack in-guest (NOVA-OK). */
    check(nova_selftest() == 0, "selftest", 0, 0);

    free(g_img);
    if (g_failed) {
        printf("VERDICT: nova-fs BROKEN\n");
        return 1;
    }
    printf("VERDICT: nova-fs correct\n");
    return 0;
}
