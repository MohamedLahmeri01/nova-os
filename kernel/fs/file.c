/* NOVA OS open-file layer (Phase 7).
 * Descriptions (node + offset + mode) referenced by per-process fds
 * 0..NOVA_NFD-1. Open resolves paths; read/write move the offset;
 * close frees the description. All bounds come from fs.h caps; every
 * failure is errno, never a panic (OOM included: fd alloc can fail).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/heap.h"
#include "fs/fs.h"
#include "syscall/syscall.h"

struct fs_file *fs_file_alloc(struct fs_node *n, uint32_t flags) {
    struct fs_file *f;
    uint32_t mode = flags & FS_O_ACCMODE;
    if (n == 0 || mode > FS_O_RDWR) {
        return 0;
    }
    f = (struct fs_file *)kmalloc(sizeof(*f));
    if (f == 0) {
        return 0;
    }
    f->node = n;
    f->off = 0;
    f->readable = (mode == FS_O_RDONLY || mode == FS_O_RDWR);
    f->writable = (mode == FS_O_WRONLY || mode == FS_O_RDWR);
    f->owns_node = 0;
    return f;
}

void fs_file_free(struct fs_file *f) {
    if (f != 0) {
        if (f->owns_node && f->node != 0) {
            if (f->node->data != 0) {
                kfree(f->node->data);
            }
            kfree(f->node);
        }
        kfree(f);
    }
}

int fs_is_disk_path(const char *kpath) {
    if (kpath == 0 || kpath[0] != '/') {
        return 0;
    }
    if (kpath[1] != 'd' || kpath[2] != 'i' || kpath[3] != 's' ||
        kpath[4] != 'k') {
        return 0;
    }
    return kpath[5] == 0 || kpath[5] == '/';
}

const char *fs_disk_rel(const char *kpath) {
    const char *r = kpath + 5;
    while (*r == '/') {
        r++;
    }
    return r;
}

int fs_is_ext_path(const char *kpath) {
    if (kpath == 0 || kpath[0] != '/') {
        return 0;
    }
    if (kpath[1] != 'e' || kpath[2] != 'x' || kpath[3] != 't') {
        return 0;
    }
    return kpath[4] == 0 || kpath[4] == '/';
}

int fs_fd_alloc(struct process *p, struct fs_file *f) {
    uint32_t i;
    if (p == 0 || f == 0) {
        return -NOVA_EINVAL;
    }
    for (i = 0; i < NOVA_NFD; i++) {
        if (p->fds[i] == 0) {
            p->fds[i] = f;
            return (int)i;
        }
    }
    return -NOVA_EMFILE;
}

struct fs_file *fs_fd_get(struct process *p, uint32_t fd) {
    if (p == 0 || fd >= NOVA_NFD || p->fds[fd] == 0) {
        return 0;
    }
    return p->fds[fd];
}

int fs_fd_drop(struct process *p, uint32_t fd) {
    if (p == 0 || fd >= NOVA_NFD || p->fds[fd] == 0) {
        return -NOVA_EBADF;
    }
    fs_file_free(p->fds[fd]);
    p->fds[fd] = 0;
    return NOVA_ESUCCESS;
}

/* --- selftest: full-stack proof with real asserts ---
 * Content round-trip (incl. a >4KB multi-grow file), nesting,
 * readdir order-independence (set membership), error discipline
 * (ENOENT/ENOTDIR/EISDIR/EBADF/EMFILE/EEXIST), O_CREAT. Leaves
 * /test/ behind as visible nesting proof for the shell. */

static int g_fail;

static void tcheck(int cond, const char *what) {
    if (!cond) {
        serial_puts("FS-FAIL ");
        serial_puts(what);
        serial_putc('\n');
        g_fail = 1;
    }
}

static int name_in(struct fs_node *dir, const char *want) {
    char buf[FS_MAX_NAME];
    uint32_t i = 0;
    for (;;) {
        int rc = fs_readdir(dir, i, buf);
        uint32_t k;
        if (rc == -NOVA_ENOENT) {
            return 0;
        }
        if (rc < 0) {
            return 0;
        }
        k = 0;
        while (want[k] != 0 && want[k] == buf[k]) {
            k++;
        }
        if (want[k] == 0 && buf[k] == 0) {
            return 1;
        }
        i++;
        if (i > 64u) {
            return 0;
        }
    }
}

int fs_selftest(void) {
    struct fs_node *n = 0;
    struct fs_node *dir = 0;
    struct fs_file *f = 0;
    /* 1KB: bigger frames trip mingw's __chkstk_ms (no libgcc on the
     * kernel link); multi-KB coverage loops over offsets instead. */
    uint8_t buf[1024];
    uint32_t got = 0;
    int rc;
    g_fail = 0;
    /* Seed content is intact. */
    rc = fs_lookup("/hello.txt", &n);
    tcheck(rc == 0 && n != 0 && !n->is_dir, "seed-hello");
    if (n != 0) {
        rc = fs_read_node(n, 0, buf, sizeof(buf), &got);
        tcheck(rc == 0 && got == 22, "seed-hello-size");
        tcheck(buf[0] == 'h' && buf[21] == '\n', "seed-hello-bytes");
        rc = fs_read_node(n, 1000, buf, sizeof(buf), &got);
        tcheck(rc == 0 && got == 0, "read-past-end");
    }
    rc = fs_lookup("/docs/readme.txt", &n);
    tcheck(rc == 0 && n != 0, "seed-nested");
    /* Seeded binary for exec (pm_main seed_binaries; absent on stale
     * images, so this also catches seed wiring regressions). */
    rc = fs_lookup("/bin/hi", &n);
    tcheck(rc == 0 && n != 0 && !n->is_dir && n->size > 0, "seed-hi");
    rc = fs_lookup("/", &dir);
    tcheck(rc == 0 && dir != 0 && dir->is_dir, "root-dir");
    tcheck(name_in(dir, "hello.txt") && name_in(dir, "docs"), "root-names");
    /* Errors, not faults. */
    rc = fs_lookup("/nope.txt", &n);
    tcheck(rc == -NOVA_ENOENT, "enoent");
    rc = fs_lookup("/hello.txt/x", &n);
    tcheck(rc == -NOVA_ENOTDIR, "enotdir");
    rc = fs_lookup("relative", &n);
    tcheck(rc == -NOVA_EINVAL, "relative");
    rc = fs_lookup("/hello.txt", &n);
    if (rc == 0) {
        tcheck(fs_read_node(n, 0, buf, 1, &got) == 0, "file-read");
        tcheck(fs_readdir(n, 0, (char *)buf) == -NOVA_ENOTDIR, "readdir-file");
        tcheck(fs_write_node(dir, 0, buf, 1, &got) == -NOVA_EISDIR,
               "write-dir");
    }
    tcheck(fs_readdir(dir, 99, (char *)buf) == -NOVA_ENOENT, "readdir-end");
    /* mkdir + create + big write (forces several doublings) + reread.
     * /test/ stays behind as visible nesting proof for the shell. */
    tcheck(fs_mkdir("/test") == 0, "mkdir");
    tcheck(fs_mkdir("/test") == -NOVA_EEXIST, "mkdir-exists");
    tcheck(fs_mkdir("/hello.txt/x") == -NOVA_ENOTDIR, "mkdir-notdir");
    tcheck(fs_create("/nothere/f.txt", &n) == -NOVA_ENOENT,
           "create-parent-missing");
    rc = fs_create("/test/big.bin", &n);
    tcheck(rc == 0 && n != 0, "create");
    tcheck(fs_create("/test/big.bin", &n) == -NOVA_EEXIST, "create-exists");
    if (n != 0) {
        uint32_t i;
        uint32_t off;
        /* 8KB total through the 1KB buffer (forces doublings). */
        for (off = 0; off < 8192u; off += sizeof(buf)) {
            for (i = 0; i < sizeof(buf); i++) {
                buf[i] = (uint8_t)((off + i) & 0xFFu);
            }
            rc = fs_write_node(n, off, buf, sizeof(buf), &got);
            if (rc != 0 || got != sizeof(buf)) {
                break;
            }
        }
        tcheck(rc == 0 && off == 8192u, "big-write");
        for (off = 0; off < 8192u; off += sizeof(buf)) {
            for (i = 0; i < sizeof(buf); i++) {
                buf[i] = 0;
            }
            rc = fs_read_node(n, off, buf, sizeof(buf), &got);
            if (rc != 0 || got != sizeof(buf)) {
                break;
            }
            for (i = 0; i < sizeof(buf); i++) {
                if (buf[i] != (uint8_t)((off + i) & 0xFFu)) {
                    break;
                }
            }
            if (i != sizeof(buf)) {
                break;
            }
        }
        tcheck(rc == 0 && off == 8192u, "big-bytes");
    }
    /* Description + fd table incl. exhaustion. */
    {
        struct process fake;
        struct fs_file *files[NOVA_NFD + 1u];
        int fds[NOVA_NFD + 1u];
        uint32_t i;
        for (i = 0; i < NOVA_NFD; i++) {
            fake.fds[i] = 0;
        }
        fake.pid = 999;
        rc = fs_lookup("/hello.txt", &n);
        tcheck(rc == 0, "fd-lookup");
        for (i = 0; i <= NOVA_NFD; i++) {
            files[i] = fs_file_alloc(n, FS_O_RDONLY);
            fds[i] = -1;
            if (files[i] != 0) {
                fds[i] = fs_fd_alloc(&fake, files[i]);
                if (fds[i] < 0) {
                    fs_file_free(files[i]);
                    files[i] = 0;
                }
            }
        }
        tcheck(fds[NOVA_NFD] == -NOVA_EMFILE, "fd-exhaust");
        tcheck(fs_fd_get(&fake, NOVA_NFD) == 0, "fd-bad-get");
        tcheck(fs_fd_drop(&fake, NOVA_NFD) == -NOVA_EBADF, "fd-bad-drop");
        if (fds[0] >= 0) {
            tcheck(fs_fd_get(&fake, (uint32_t)fds[0]) != 0, "fd-get");
            tcheck(fs_fd_drop(&fake, (uint32_t)fds[0]) == 0, "fd-drop");
            tcheck(fs_fd_get(&fake, (uint32_t)fds[0]) == 0, "fd-gone");
            /* Slot reuse after close. */
            f = fs_file_alloc(n, FS_O_RDONLY);
            tcheck(f != 0 && fs_fd_alloc(&fake, f) == fds[0], "fd-reuse");
            if (f != 0) {
                fs_fd_drop(&fake, (uint32_t)fds[0]);
            }
        }
        for (i = 1; i < NOVA_NFD; i++) {
            if (fds[i] >= 0) {
                fs_fd_drop(&fake, (uint32_t)fds[i]);
            }
        }
    }
    if (g_fail) {
        return -1;
    }
    serial_puts("FS-OK\n");
    return NOVA_ESUCCESS;
}
