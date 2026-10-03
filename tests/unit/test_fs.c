/* NOVA OS filesystem host test (Phase 7, T9).
 * Host-side: the REAL kernel/fs/ramfs.c + kernel/fs/file.c against
 * stub kmalloc (malloc) and stub serial/panic. Deterministic checks
 * (seed content, nesting, dot-components, error discipline, 64KB cap,
 * gap zeroing, fd exhaustion/reuse) plus a randomized path fuzz for
 * crash-freedom. VERDICT line on success (mirrors T7/T8 style).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "mm/heap.h"
#include "process/process.h"
#include "fs/fs.h"
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

void *kmalloc(uint32_t size) {
    if (size == 0) {
        return 0;
    }
    return malloc((size_t)size);
}

void kfree(void *ptr) {
    free(ptr);
}

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

static uint32_t lcg_state = 0x12345678u;

static uint32_t lcg(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

int main(void) {
    struct fs_node *n = 0;
    uint8_t buf[1024];
    uint8_t big[70000];
    uint32_t got = 0;
    char name[FS_MAX_NAME];
    int rc;

    check(fs_init() == 0, "init", 0, 0);

    /* Mirror pm_main's seed_binaries (guest-only blob wiring): the
     * selftest asserts /bin/hi exists, so the host seeds a dummy. */
    {
        struct fs_node *hi = 0;
        uint32_t got = 0;
        uint8_t tiny[16];
        memset(tiny, 0x90, sizeof(tiny));
        check(fs_mkdir("/bin") == 0, "host-bin", 0, 0);
        check(fs_create("/bin/hi", &hi) == 0 && hi != 0, "host-hi", 0,
              0);
        if (hi != 0) {
            check(fs_write_node(hi, 0, tiny, sizeof(tiny), &got) == 0 &&
                      got == sizeof(tiny),
                  "host-seed", got, 0);
        }
    }

    /* Seed content. */
    rc = fs_lookup("/hello.txt", &n);
    check(rc == 0 && n != 0 && !n->is_dir, "seed", (unsigned long)rc, 0);
    rc = fs_read_node(n, 0, buf, sizeof(buf), &got);
    check(rc == 0 && got == 22 && memcmp(buf, "hello from nova ramfs\n",
                                         22) == 0,
          "seed-bytes", got, 0);

    /* Path normalization. */
    rc = fs_lookup("//hello.txt", &n);
    check(rc == 0, "dbl-slash", (unsigned long)rc, 0);
    rc = fs_lookup("/docs/../hello.txt", &n);
    check(rc == 0, "dotdot", (unsigned long)rc, 0);
    rc = fs_lookup("/./docs/./readme.txt", &n);
    check(rc == 0, "dots", (unsigned long)rc, 0);
    rc = fs_lookup("/docs/", &n);
    check(rc == 0 && n != 0 && n->is_dir, "trail-slash",
          (unsigned long)rc, 0);
    rc = fs_lookup("/", &n);
    check(rc == 0 && n != 0 && n->is_dir, "root", (unsigned long)rc, 0);
    rc = fs_lookup("/../..", &n);
    check(rc == 0 && n != 0 && n->is_dir, "dotdot-root",
          (unsigned long)rc, 0);

    /* Errors. */
    check(fs_lookup("/missing", &n) == -NOVA_ENOENT, "enoent", 0, 0);
    check(fs_lookup("/hello.txt/deep", &n) == -NOVA_ENOTDIR, "enotdir",
          0, 0);
    check(fs_lookup("rel", &n) == -NOVA_EINVAL, "rel", 0, 0);
    check(fs_mkdir("/hello.txt/kid") == -NOVA_ENOTDIR, "mkdir-notdir",
          0, 0);

    /* Nesting + mkdir -p depth. */
    check(fs_mkdir("/a") == 0, "mkdir-a", 0, 0);
    check(fs_mkdir("/a/b") == 0, "mkdir-b", 0, 0);
    check(fs_mkdir("/a/b/c") == 0, "mkdir-c", 0, 0);
    rc = fs_create("/a/b/c/f.txt", &n);
    check(rc == 0 && n != 0, "deep-create", (unsigned long)rc, 0);
    rc = fs_lookup("/a/./b/../b/c/f.txt", &n);
    check(rc == 0, "deep-dots", (unsigned long)rc, 0);

    /* Write at offset leaves zeroed gap. */
    rc = fs_create("/gap.bin", &n);
    check(rc == 0, "gap-create", (unsigned long)rc, 0);
    memset(buf, 0xAB, sizeof(buf));
    rc = fs_write_node(n, 100, buf, 10, &got);
    check(rc == 0 && got == 10 && n->size == 110, "gap-write", got,
          n ? n->size : 0);
    memset(buf, 0xFF, sizeof(buf));
    rc = fs_read_node(n, 0, buf, 110, &got);
    check(rc == 0 && got == 110, "gap-reread", got, 0);
    {
        int ok = 1;
        for (uint32_t i = 0; i < 100; i++) {
            if (buf[i] != 0) {
                ok = 0;
                break;
            }
        }
        for (uint32_t i = 100; i < 110; i++) {
            if (buf[i] != 0xAB) {
                ok = 0;
                break;
            }
        }
        check(ok, "gap-zero", 0, 0);
    }

    /* 64KB cap enforced. */
    rc = fs_create("/cap.bin", &n);
    check(rc == 0, "cap-create", (unsigned long)rc, 0);
    memset(big, 0x5A, sizeof(big));
    rc = fs_write_node(n, 0, big, sizeof(big), &got);
    check(rc == -NOVA_ENOSPC, "cap-deny", (unsigned long)rc, 0);
    rc = fs_write_node(n, 0, big, FS_MAX_FILE, &got);
    check(rc == 0 && got == FS_MAX_FILE, "cap-exact", got, 0);
    rc = fs_write_node(n, FS_MAX_FILE, big, 1, &got);
    check(rc == -NOVA_ENOSPC, "cap-over", (unsigned long)rc, 0);

    /* readdir counts + termination. */
    {
        struct fs_node *dir = 0;
        uint32_t count = 0;
        check(fs_lookup("/a/b", &dir) == 0, "rd-dir", 0, 0);
        for (;;) {
            rc = fs_readdir(dir, count, name);
            if (rc == -NOVA_ENOENT) {
                break;
            }
            check(rc > 0, "rd-len", (unsigned long)rc, count);
            count++;
            if (count > 64u) {
                break;
            }
        }
        check(count == 1 && strcmp(name, "c") == 0, "rd-one", count, 0);
    }

    /* Randomized path fuzz (crash-freedom + errno discipline). */
    {
        static const char *comps[] = {
            "a", "b", "c", "f.txt", "hello.txt", "docs", "..", ".",
            "", "gap.bin", "nope", "x"
        };
        for (int t = 0; t < 20000; t++) {
            char p[64];
            uint32_t at = 0;
            int ncomp = 1 + (int)(lcg() % 4u);
            p[at++] = '/';
            for (int k = 0; k < ncomp; k++) {
                const char *c = comps[lcg() % 12u];
                size_t L = strlen(c);
                if (at + L + 1 >= sizeof(p)) {
                    break;
                }
                memcpy(p + at, c, L);
                at += (uint32_t)L;
                p[at++] = '/';
            }
            p[at] = 0;
            rc = fs_lookup(p, &n);
            check(rc == 0 || rc == -NOVA_ENOENT || rc == -NOVA_ENOTDIR ||
                      rc == -NOVA_EINVAL,
                  "fuzz-errno", (unsigned long)rc, (unsigned long)t);
            if (rc == 0 && n != 0 && !n->is_dir) {
                uint32_t g2 = 0;
                uint8_t sb[16];
                int r2 = fs_read_node(n, 0, sb, sizeof(sb), &g2);
                check(r2 == 0, "fuzz-read", (unsigned long)r2, 0);
            }
        }
        printf("fuzz: 20000 randomized paths checked\n");
    }

    /* Guest selftest runs the same stack in-guest (asserts + FS-OK). */
    check(fs_selftest() == 0, "selftest", 0, 0);

    if (g_failed) {
        printf("VERDICT: filesystem BROKEN\n");
        return 1;
    }
    printf("VERDICT: filesystem correct\n");
    return 0;
}
