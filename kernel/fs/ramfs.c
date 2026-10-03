/* NOVA OS RAMFS (Phase 7).
 * In-memory tree: static root "/", dirs link children, files own a
 * kmalloc'd buffer grown by doubling (cap FS_MAX_FILE). Lookup walks
 * one component at a time (bounded depth, "." and ".." resolved,
 * empty components skipped). No locking yet (single-core syscalls;
 * SMP adds it with the scheduler). Host-testable: only kmalloc/kfree
 * + bounded local helpers (no port I/O, no tables).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/heap.h"
#include "fs/fs.h"
#include "syscall/syscall.h"

static struct fs_node g_root;

static uint32_t fs_strlen(const char *s, uint32_t cap) {
    uint32_t n = 0;
    while (n < cap && s[n] != 0) {
        n++;
    }
    return n;
}

static int fs_name_eq(const char *a, const char *b) {
    for (uint32_t i = 0; i < FS_MAX_NAME; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
    }
    return 1;
}

static void fs_name_copy(char *dst, const char *src, uint32_t len) {
    uint32_t i = 0;
    while (i + 1u < FS_MAX_NAME && i < len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void fs_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

static struct fs_node *node_new(const char *name, uint32_t len, int dir) {
    struct fs_node *n = (struct fs_node *)kmalloc(sizeof(*n));
    uint32_t i;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < FS_MAX_NAME; i++) {
        n->name[i] = 0;
    }
    fs_name_copy(n->name, name, len);
    n->is_dir = dir;
    n->parent = 0;
    n->child = 0;
    n->sibling = 0;
    n->data = 0;
    n->size = 0;
    n->cap = 0;
    return n;
}

static void node_link(struct fs_node *parent, struct fs_node *n) {
    n->parent = parent;
    n->sibling = parent->child;
    parent->child = n;
}

/* Split off the next component: *pp advances past it (and slashes).
 * Returns component length (0 = end of path). */
static uint32_t next_comp(const char **pp) {
    const char *p = *pp;
    const char *s;
    while (*p == '/') {
        p++;
    }
    s = p;
    while (*p != 0 && *p != '/') {
        p++;
    }
    *pp = p;
    return (uint32_t)(p - s);
}

static struct fs_node *child_named(struct fs_node *dir, const char *name,
                                   uint32_t len) {
    struct fs_node *c;
    char tmp[FS_MAX_NAME];
    if (!dir->is_dir || len == 0 || len >= FS_MAX_NAME) {
        return 0;
    }
    fs_name_copy(tmp, name, len);
    for (c = dir->child; c != 0; c = c->sibling) {
        if (fs_name_eq(c->name, tmp)) {
            return c;
        }
    }
    return 0;
}

int fs_lookup(const char *path, struct fs_node **out) {
    const char *p;
    struct fs_node *cur;
    uint32_t depth = 0;
    if (path == 0 || out == 0 || path[0] != '/') {
        return -NOVA_EINVAL;
    }
    if (fs_strlen(path, FS_MAX_PATH + 1u) > FS_MAX_PATH) {
        return -NOVA_ENAMETOOLONG;
    }
    cur = &g_root;
    p = path;
    for (;;) {
        const char *s;
        uint32_t len;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            *out = cur;
            return NOVA_ESUCCESS;
        }
        if (++depth > FS_MAX_DEPTH) {
            return -NOVA_ENAMETOOLONG;
        }
        s = p;
        len = next_comp(&p);
        if (len == 1 && s[0] == '.') {
            continue;
        }
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (cur->parent != 0) {
                cur = cur->parent;
            }
            continue;
        }
        if (!cur->is_dir) {
            return -NOVA_ENOTDIR;
        }
        cur = child_named(cur, s, len);
        if (cur == 0) {
            return -NOVA_ENOENT;
        }
    }
}

static int resolve_parent(const char *path, struct fs_node **dir_out,
                          const char **last_out, uint32_t *len_out);

int fs_create(const char *path, struct fs_node **out) {
    struct fs_node *dir;
    struct fs_node *n;
    const char *last;
    uint32_t last_len;
    int rc = resolve_parent(path, &dir, &last, &last_len);
    if (rc != 0) {
        return rc;
    }
    if (child_named(dir, last, last_len) != 0) {
        return -NOVA_EEXIST;
    }
    n = node_new(last, last_len, 0);
    if (n == 0) {
        return -NOVA_ENOSPC;
    }
    node_link(dir, n);
    if (out != 0) {
        *out = n;
    }
    return NOVA_ESUCCESS;
}

int fs_mkdir(const char *path) {
    struct fs_node *dir;
    struct fs_node *n;
    const char *last;
    uint32_t last_len;
    int rc = resolve_parent(path, &dir, &last, &last_len);
    if (rc != 0) {
        return rc;
    }
    if (child_named(dir, last, last_len) != 0) {
        return -NOVA_EEXIST;
    }
    n = node_new(last, last_len, 1);
    if (n == 0) {
        return -NOVA_ENOSPC;
    }
    node_link(dir, n);
    return NOVA_ESUCCESS;
}

/* Parent = whole path minus the final component: resolve the parent
 * dir and point at the final component (shared by create/mkdir). */
static int resolve_parent(const char *path, struct fs_node **dir_out,
                          const char **last_out, uint32_t *len_out) {
    const char *p;
    const char *last = 0;
    uint32_t last_len = 0;
    struct fs_node *dir = 0;
    char tmp[FS_MAX_PATH + 1u];
    uint32_t plen;
    if (path == 0 || path[0] != '/' || dir_out == 0 || last_out == 0 ||
        len_out == 0) {
        return -NOVA_EINVAL;
    }
    plen = fs_strlen(path, FS_MAX_PATH + 1u);
    if (plen > FS_MAX_PATH || plen == 0) {
        return -NOVA_ENAMETOOLONG;
    }
    /* Parent = whole path minus the final component. */
    p = path;
    for (;;) {
        const char *s;
        uint32_t len;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        s = p;
        len = next_comp(&p);
        last = s;
        last_len = len;
    }
    if (last == 0 || last_len == 0 || last_len >= FS_MAX_NAME) {
        return -NOVA_EINVAL;
    }
    if (last_len == 1 && last[0] == '.') {
        return -NOVA_EINVAL;
    }
    if (last_len == 2 && last[0] == '.' && last[1] == '.') {
        return -NOVA_EINVAL;
    }
    /* Copy the parent prefix (everything before the final comp). */
    {
        uint32_t npre = (uint32_t)(last - path);
        uint32_t i = 0;
        while (i < npre && i < FS_MAX_PATH) {
            tmp[i] = path[i];
            i++;
        }
        tmp[i] = 0;
        if (i == 0) {
            tmp[0] = '/';
            tmp[1] = 0;
        }
    }
    if (fs_lookup(tmp, &dir) != 0) {
        return -NOVA_ENOENT;
    }
    if (!dir->is_dir) {
        return -NOVA_ENOTDIR;
    }
    *dir_out = dir;
    *last_out = last;
    *len_out = last_len;
    return NOVA_ESUCCESS;
}

static int grow_to(struct fs_node *n, uint32_t need) {
    uint32_t cap = (n->cap != 0) ? n->cap : 64u;
    uint8_t *nd;
    if (need > FS_MAX_FILE) {
        return -NOVA_ENOSPC;
    }
    while (cap < need) {
        cap *= 2u;
    }
    if (cap > FS_MAX_FILE) {
        cap = FS_MAX_FILE;
    }
    nd = (uint8_t *)kmalloc(cap);
    if (nd == 0) {
        return -NOVA_ENOSPC;
    }
    for (uint32_t i = 0; i < n->size; i++) {
        nd[i] = n->data[i];
    }
    if (n->data != 0) {
        kfree(n->data);
    }
    n->data = nd;
    n->cap = cap;
    return NOVA_ESUCCESS;
}

int fs_read_node(struct fs_node *n, uint32_t off, uint8_t *dst,
                 uint32_t len, uint32_t *n_out) {
    uint32_t avail;
    if (n == 0 || dst == 0 || n_out == 0) {
        return -NOVA_EINVAL;
    }
    if (n->is_dir) {
        return -NOVA_EISDIR;
    }
    if (off >= n->size) {
        *n_out = 0;
        return NOVA_ESUCCESS;
    }
    avail = n->size - off;
    if (len > avail) {
        len = avail;
    }
    fs_memcpy(dst, n->data + off, len);
    *n_out = len;
    return NOVA_ESUCCESS;
}

int fs_write_node(struct fs_node *n, uint32_t off, const uint8_t *src,
                  uint32_t len, uint32_t *n_out) {
    uint32_t end;
    int rc;
    if (n == 0 || (src == 0 && len != 0) || n_out == 0) {
        return -NOVA_EINVAL;
    }
    if (n->is_dir) {
        return -NOVA_EISDIR;
    }
    end = off + len;
    if (end < off || end > FS_MAX_FILE) {
        return -NOVA_ENOSPC;
    }
    if (end > n->cap) {
        /* Atomic write: all bytes or an error (no partial grows). */
        rc = grow_to(n, end);
        if (rc != 0) {
            *n_out = 0;
            return rc;
        }
    }
    fs_memcpy(n->data + off, src, len);
    if (end > n->size) {
        /* Zero any gap between old size and the write offset. */
        for (uint32_t i = n->size; i < off; i++) {
            n->data[i] = 0;
        }
        n->size = end;
    }
    *n_out = len;
    return NOVA_ESUCCESS;
}

int fs_readdir(struct fs_node *dir, uint32_t index, char *name_out) {
    struct fs_node *c;
    uint32_t i = 0;
    if (dir == 0 || name_out == 0) {
        return -NOVA_EINVAL;
    }
    if (!dir->is_dir) {
        return -NOVA_ENOTDIR;
    }
    for (c = dir->child; c != 0; c = c->sibling) {
        if (i == index) {
            uint32_t k = 0;
            while (k + 1u < FS_MAX_NAME && c->name[k] != 0) {
                name_out[k] = c->name[k];
                k++;
            }
            name_out[k] = 0;
            return (int)k;
        }
        i++;
    }
    return -NOVA_ENOENT;
}

static int seed_file(const char *path, const char *text) {
    struct fs_node *n;
    uint32_t got = 0;
    int rc = fs_create(path, &n);
    uint32_t len;
    if (rc != 0) {
        return rc;
    }
    len = fs_strlen(text, FS_MAX_FILE);
    return fs_write_node(n, 0, (const uint8_t *)text, len, &got);
}

static struct fs_node *mkdir_p(const char *path) {
    struct fs_node *cur = &g_root;
    const char *p = path;
    uint32_t depth = 0;
    if (path == 0 || path[0] != '/') {
        return 0;
    }
    for (;;) {
        const char *s;
        uint32_t len;
        struct fs_node *next;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            return cur;
        }
        if (++depth > FS_MAX_DEPTH) {
            return 0;
        }
        s = p;
        len = next_comp(&p);
        if (len == 0 || len >= FS_MAX_NAME) {
            return 0;
        }
        next = child_named(cur, s, len);
        if (next != 0) {
            if (!next->is_dir) {
                return 0;
            }
            cur = next;
            continue;
        }
        next = node_new(s, len, 1);
        if (next == 0) {
            return 0;
        }
        node_link(cur, next);
        cur = next;
    }
}

int fs_init(void) {
    uint32_t i;
    for (i = 0; i < FS_MAX_NAME; i++) {
        g_root.name[i] = 0;
    }
    g_root.name[0] = '/';
    g_root.is_dir = 1;
    g_root.parent = 0;
    g_root.child = 0;
    g_root.sibling = 0;
    g_root.data = 0;
    g_root.size = 0;
    g_root.cap = 0;
    if (mkdir_p("/docs") == 0) {
        return -NOVA_ENOSPC;
    }
    if (seed_file("/hello.txt", "hello from nova ramfs\n") != 0) {
        return -NOVA_ENOSPC;
    }
    if (seed_file("/docs/readme.txt",
                  "nova ramfs: VFS demo.\ntry: ls /docs ; cat /hello.txt\n") !=
        0) {
        return -NOVA_ENOSPC;
    }
    return NOVA_ESUCCESS;
}
