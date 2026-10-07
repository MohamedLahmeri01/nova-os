/* NOVA OS syscall dispatch + handlers (Phase 5 ABI v1, Phase 7 files).
 * Table-driven (new versions add rows). Every pointer/length goes
 * through validate_usermem, and every payload movement through
 * copy_from/to_user (single canonical policy + copies, except the
 * NUL-scanning copy_user_path); failures return -errno, never
 * panic. Handler costs are bounded (print snapshots then emits;
 * file I/O chunks through a 512B bounce buffer).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "syscall/syscall.h"
#include "syscall/validate.h"
#include "process/caps.h"
#include "process/rlimit.h"
#include "gui/comp.h"
#include "drivers/fb.h"
#include "net/sock.h"
#include "nova/types.h"
#include "mm/addrspace.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "fs/fs.h"
#include "fs/fat32.h"
#include "fs/ext4.h"
#include "fs/nova.h"
#include "net/net.h"
#include "irq/irq.h"
#include "input/kbd.h"
#include "trace/trace.h"
#include "time/time.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#include "../arch/x86/io.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/io.h"
#else
#error "syscall: no arch backend for this architecture"
#endif

extern void syscall_stub(void);

static uint32_t g_sysc_yield;
static uint32_t g_sysc_print;
static uint32_t g_sysc_exit;

void syscall_stats(uint32_t *yld, uint32_t *prt, uint32_t *ext) {
    if (yld) {
        *yld = g_sysc_yield;
    }
    if (prt) {
        *prt = g_sysc_print;
    }
    if (ext) {
        *ext = g_sysc_exit;
    }
}

static int32_t do_yield(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    g_sysc_yield++;
    sched_yield();
    return NOVA_ESUCCESS;
}

static int32_t do_exit(uint32_t code, uint32_t b, uint32_t c) {
    struct thread *cur;
    struct thread *next;
    (void)b;
    (void)c;
    g_sysc_exit++;
    cur = sched_current_thread();
    if (cur == 0) {
        return -NOVA_EINVAL;
    }
    if (cur->is_user) {
        if (cur->user_stack != 0) {
            pmm_free_contig(cur->user_stack, 2);
        }
        if (cur->user_shared != 0) {
            pmm_free_frame(cur->user_shared);
        }
    }
    (void)code;
    thread_zombie_handoff(cur);
    next = sched_pick(cur);
    if (next == 0 || next == cur) {
        nova_panic("sched-empty");
    }
    sched_switch_to(cur, next);
    nova_panic("sys-exit-returned");
}

static int32_t do_print(uint32_t ptr, uint32_t len, uint32_t c) {
    /* Stack snapshot (per-thread: never shared like a static;
     * 1KB of the 8KB kstack). */
    uint8_t kbuf[SYSCALL_MAX_PRINT];
    int rc;
    (void)c;
    g_sysc_print++;
    if (len > SYSCALL_MAX_PRINT) {
        return -NOVA_EINVAL;
    }
    /* Snapshot first (single-pass user read), then emit: the
     * serial loop never touches user memory (and holds cli for
     * fewer, kernel-only stores). */
    rc = copy_from_user(kbuf, ptr, len);
    if (rc != 0) {
        return rc;
    }
    __asm__ volatile("cli" ::: "memory");
    for (uint32_t i = 0; i < len; i++) {
        serial_putc((char)kbuf[i]);
    }
    __asm__ volatile("sti" ::: "memory");
    return (int32_t)len;
}

static int32_t do_getpid(uint32_t a, uint32_t b, uint32_t c) {
    struct thread *cur;
    (void)a;
    (void)b;
    (void)c;
    cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return -NOVA_EINVAL;
    }
    return (int32_t)cur->proc->pid;
}

static int32_t do_gettid(uint32_t a, uint32_t b, uint32_t c) {
    struct thread *cur;
    (void)a;
    (void)b;
    (void)c;
    cur = sched_current_thread();
    if (cur == 0) {
        return -NOVA_EINVAL;
    }
    return (int32_t)cur->id;
}

static int32_t do_version(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return (int32_t)NOVA_ABI_VERSION;
}

static int32_t do_getkey(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return kbd_getkey();
}

static int32_t do_meminfo(uint32_t total_ptr, uint32_t free_ptr,
                          uint32_t c) {
    uint32_t *t;
    uint32_t *f;
    (void)c;
    if (validate_usermem(total_ptr, 4, 1) != 0 ||
        validate_usermem(free_ptr, 4, 1) != 0) {
        return -NOVA_EFAULT;
    }
    t = (uint32_t *)total_ptr;
    f = (uint32_t *)free_ptr;
    *t = pmm_total_frames();
    *f = pmm_free_count();
    return NOVA_ESUCCESS;
}

static int32_t do_ticks(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    return (int32_t)g_ticks;
}

/* Copy a user path into a kernel buffer (validated read, NUL found
 * within FS_MAX_PATH). Returns length or negative -errno. */
static int32_t copy_user_path(uint32_t uptr, char *kbuf) {
    uint32_t len;
    if (validate_usermem(uptr, 1, 0) != 0) {
        return -NOVA_EFAULT;
    }
    for (len = 0; len <= FS_MAX_PATH; len++) {
        uint32_t a = uptr + len;
        if (validate_usermem(a, 1, 0) != 0) {
            return -NOVA_EFAULT;
        }
        kbuf[len] = *(volatile char *)a;
        if (kbuf[len] == 0) {
            return (int32_t)len;
        }
    }
    return -NOVA_ENAMETOOLONG;
}

static struct process *sysc_proc(void) {
    struct thread *cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return 0;
    }
    return cur->proc;
}

/* Attach a materialized whole-file node (detached, owned) as an fd.
 * `store_path` enables write-through (FAT); NULL = read-only copy
 * (EXT4). Frees node/data on failure. Returns fd or -errno. */
static int32_t open_attach(struct process *p, struct fs_node *n,
                           uint8_t *data, uint32_t size, uint32_t flags,
                           const char *store_path) {
    struct fs_file *f;
    uint32_t pi = 0;
    int rc;
    n->size = size;
    f = fs_file_alloc(n, flags);
    if (f == 0) {
        kfree(n);
        if (data != 0) {
            kfree(data);
        }
        return -NOVA_ENOSPC;
    }
    /* Permission gate (Phase 10c): the materialized node is
     * opener-owned by construction, so this passes vacuously
     * today; it binds the moment foreign meta arrives. */
    rc = fs_open_check(p, n, flags);
    if (rc != 0) {
        fs_file_free(f);
        return rc;
    }
    f->owns_node = 1;
    for (uint32_t i = 0; i <= FS_MAX_PATH; i++) {
        f->store_path[i] = 0;
    }
    if (store_path != 0) {
        while (pi <= FS_MAX_PATH) {
            f->store_path[pi] = store_path[pi];
            if (store_path[pi] == 0) {
                break;
            }
            pi++;
        }
        f->store_path[FS_MAX_PATH] = 0;
    }
    rc = fs_fd_alloc(p, f);
    if (rc < 0) {
        fs_file_free(f);
        return rc;
    }
    return rc;
}

/* Blank detached node for materialization (data buffer separate). */
static struct fs_node *blank_node(uint8_t *data, uint32_t cap) {
    struct fs_node *n = (struct fs_node *)kmalloc(sizeof(*n));
    if (n == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < FS_MAX_NAME; i++) {
        n->name[i] = 0;
    }
    n->is_dir = 0;
    n->parent = 0;
    n->child = 0;
    n->sibling = 0;
    n->data = data;
    n->size = 0;
    n->cap = cap;
    /* Opener-owned view (Phase 10c, vfat-style): foreign
     * filesystems carry no identity, so the opener owns the copy
     * (open checks below pass vacuously for it, by construction). */
    n->uid = fs_current_uid();
    n->gid = fs_current_gid();
    n->mode = 0644;
    return n;
}

static int32_t do_open(uint32_t path, uint32_t flags, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    struct fs_node *n = 0;
    struct fs_file *f;
    struct process *p;
    int32_t plen;
    int rc;
    (void)c;
    if ((flags & ~((uint32_t)(FS_O_ACCMODE | FS_O_CREAT))) != 0) {
        return -NOVA_EINVAL;
    }
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    /* Graft namespaces are global ground (Phase 10d): jailed
     * callers skip every graft branch below and land in the ramfs
     * tail, where the confined lookup finds no "disk"/"ext"/"nova"
     * (ENOENT: invisible, not refused). */
    /* /disk graft (Phase 7c/d): FAT files materialize into a detached
     * node owned by the description. Writes go through to the disk
     * (write-through via the stored path); reads use the node copy. */
    if (p->jail_root == 0 && fs_is_disk_path(kpath)) {
        uint8_t *data;
        uint32_t size = 0;
        uint32_t got = 0;
        int is_dir = 0;
        struct fs_node *n;
        rc = fat_stat(kpath, &size, &is_dir);
        if (rc == -NOVA_ENOENT && (flags & FS_O_CREAT) != 0) {
            rc = fat_create_file(kpath);
            if (rc == 0) {
                size = 0;
                is_dir = 0;
            }
        }
        if (rc != 0) {
            return rc;
        }
        if (is_dir) {
            return -NOVA_EISDIR;
        }
        if (size > FS_MAX_FILE) {
            return -NOVA_ENOSPC;
        }
        data = (size != 0) ? (uint8_t *)kmalloc(size) : 0;
        if (size != 0 && data == 0) {
            return -NOVA_ENOSPC;
        }
        n = blank_node(data, size);
        if (n == 0) {
            if (data != 0) {
                kfree(data);
            }
            return -NOVA_ENOSPC;
        }
        if (size == 0) {
            /* Empty file: no bytes to fetch (and no buffer). */
            got = 0;
            rc = 0;
        } else {
            rc = fat_read_file(kpath, data, size, &got);
        }
        if (rc != 0 || got != size) {
            kfree(n);
            if (data != 0) {
                kfree(data);
            }
            return (rc != 0) ? rc : -NOVA_EIO;
        }
        return open_attach(p, n, data, size, flags, kpath);
    }
    /* EXT4 graft (Phase 7e): read-only materialize, no stored path. */
    if (p->jail_root == 0 && fs_is_ext_path(kpath)) {
        uint8_t *data;
        uint32_t size = 0;
        uint32_t got = 0;
        int is_dir = 0;
        struct fs_node *n;
        if ((flags & FS_O_ACCMODE) != FS_O_RDONLY) {
            return -NOVA_EROFS;
        }
        rc = ext_stat(kpath, &size, &is_dir);
        if (rc != 0) {
            return rc;
        }
        if (is_dir) {
            return -NOVA_EISDIR;
        }
        if (size > FS_MAX_FILE) {
            return -NOVA_ENOSPC;
        }
        data = (size != 0) ? (uint8_t *)kmalloc(size) : 0;
        if (size != 0 && data == 0) {
            return -NOVA_ENOSPC;
        }
        n = blank_node(data, size);
        if (n == 0) {
            if (data != 0) {
                kfree(data);
            }
            return -NOVA_ENOSPC;
        }
        if (size == 0) {
            got = 0;
            rc = 0;
        } else {
            rc = ext_read_file(kpath, data, size, &got);
        }
        if (rc != 0 || got != size) {
            kfree(n);
            if (data != 0) {
                kfree(data);
            }
            return (rc != 0) ? rc : -NOVA_EIO;
        }
        /* Native identity (Phase 13c): the opener-owned blank node
         * takes the on-disk uid/gid/mode (fail-closed: good data
         * with unreadable identity refuses). */
        {
            uint32_t ouid = 0, ogid = 0;
            uint16_t omode = 0;
            if (ext_inode_info(kpath, &ouid, &ogid, &omode) != 0) {
                kfree(n);
                if (data != 0) {
                    kfree(data);
                }
                return -NOVA_EIO;
            }
            n->uid = ouid;
            n->gid = ogid;
            n->mode = omode;
        }
        return open_attach(p, n, data, size, flags, 0);
    }
    /* NOVA-FS graft (Phase 7f-2): writable materialize with stored
     * path (write-through like FAT). CREAT makes empty files. */
    if (p->jail_root == 0 && fs_is_nova_path(kpath)) {
        uint8_t *data;
        uint32_t size = 0;
        uint32_t got = 0;
        int is_dir = 0;
        struct fs_node *n;
        rc = nova_stat(kpath, &size, &is_dir);
        if (rc == -NOVA_ENOENT && (flags & FS_O_CREAT) != 0) {
            rc = nova_create_file(kpath);
            if (rc == 0) {
                size = 0;
                is_dir = 0;
            }
        }
        if (rc != 0) {
            return rc;
        }
        if (is_dir) {
            return -NOVA_EISDIR;
        }
        if (size > FS_MAX_FILE) {
            return -NOVA_ENOSPC;
        }
        data = (size != 0) ? (uint8_t *)kmalloc(size) : 0;
        if (size != 0 && data == 0) {
            return -NOVA_ENOSPC;
        }
        n = blank_node(data, size);
        if (n == 0) {
            if (data != 0) {
                kfree(data);
            }
            return -NOVA_ENOSPC;
        }
        if (size == 0) {
            got = 0;
            rc = 0;
        } else {
            rc = nova_read_file(kpath, data, size, &got);
        }
        if (rc != 0 || got != size) {
            kfree(n);
            if (data != 0) {
                kfree(data);
            }
            return (rc != 0) ? rc : -NOVA_EIO;
        }
        /* Native identity (Phase 13c): newly created files were
         * just stamped with the creator (create path), so the
         * inode read agrees; older files carry their own. */
        {
            uint32_t ouid = 0, ogid = 0;
            uint16_t omode = 0;
            if (nova_inode_info(kpath, &ouid, &ogid, &omode) != 0) {
                kfree(n);
                if (data != 0) {
                    kfree(data);
                }
                return -NOVA_EIO;
            }
            n->uid = ouid;
            n->gid = ogid;
            n->mode = omode;
        }
        return open_attach(p, n, data, size, flags, kpath);
    }
    rc = fs_lookup(kpath, &n);
    if (rc == -NOVA_ENOENT && (flags & FS_O_CREAT) != 0) {
        rc = fs_create(kpath, &n);
    }
    if (rc != 0) {
        return rc;
    }
    /* Permission before type (Phase 10c, fail-closed): denials
     * reveal nothing, not even dir-ness. */
    rc = fs_open_check(p, n, flags);
    if (rc != 0) {
        return rc;
    }
    if (n->is_dir) {
        return -NOVA_EISDIR;
    }
    f = fs_file_alloc(n, flags);
    if (f == 0) {
        return -NOVA_ENOSPC;
    }
    rc = fs_fd_alloc(p, f);
    if (rc < 0) {
        fs_file_free(f);
        return rc;
    }
    return rc;
}

static int32_t do_read(uint32_t fd, uint32_t buf, uint32_t len) {
    struct process *p;
    struct fs_file *f;
    uint8_t kbuf[512];
    uint32_t total = 0;
    if (len > FS_MAX_FILE) {
        return -NOVA_EINVAL;
    }
    if (len != 0 && validate_usermem(buf, len, 1) != 0) {
        return -NOVA_EFAULT;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    f = fs_fd_get(p, fd);
    if (f == 0) {
        return -NOVA_EBADF;
    }
    if (!f->readable) {
        return -NOVA_EBADF;
    }
    /* Chunked through a kernel bounce buffer (user pages may be
     * non-contiguous; 512B keeps IRQ latency bounded). */
    while (total < len) {
        uint32_t want = len - total;
        uint32_t got = 0;
        int rc;
        if (want > sizeof(kbuf)) {
            want = sizeof(kbuf);
        }
        rc = fs_read_node(f->node, f->off, kbuf, want, &got);
        if (rc != 0) {
            return (total != 0) ? (int32_t)total : rc;
        }
        if (got == 0) {
            break;
        }
        rc = copy_to_user(buf + total, kbuf, got);
        if (rc != 0) {
            return (total != 0) ? (int32_t)total : rc;
        }
        f->off += got;
        total += got;
    }
    return (int32_t)total;
}

static int32_t do_write(uint32_t fd, uint32_t buf, uint32_t len) {
    struct process *p;
    struct fs_file *f;
    uint8_t kbuf[512];
    uint32_t total = 0;
    if (len > FS_MAX_FILE) {
        return -NOVA_EINVAL;
    }
    if (len != 0 && validate_usermem(buf, len, 0) != 0) {
        return -NOVA_EFAULT;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    f = fs_fd_get(p, fd);
    if (f == 0) {
        return -NOVA_EBADF;
    }
    if (!f->writable) {
        return -NOVA_EBADF;
    }
    /* File-size ceiling (Phase 10b): clamp the request to the
     * remaining allowance (partial writes allowed up to the
     * limit); EFBIG when already at/past it. Empty writes skip
     * the gate (always succeed, like before). */
    if (len != 0) {
        uint32_t oklen = len;
        int r = rlimit_fsize_allow(p->rlimit_fsize, f->off, len,
                                   &oklen);
        if (r != 0) {
            return r;
        }
        len = oklen;
    }
    /* FAT write-through (Phase 7d): node copy is a cache; the disk is
     * truth. Grow the node buffer first (OOM fails before any disk
     * mutation), then FAT, then refresh the cache copy. EXT4 files
     * carry no stored path (read-only): refuse with EROFS. */
    if (f->owns_node) {
        if (f->store_path[0] == 0) {
            return -NOVA_EROFS;
        }
        while (total < len) {
            uint32_t want = len - total;
            int rc;
            uint32_t end;
            if (want > sizeof(kbuf)) {
                want = sizeof(kbuf);
            }
            rc = copy_from_user(kbuf, buf + total, want);
            if (rc != 0) {
                return (total != 0) ? (int32_t)total : rc;
            }
            end = f->off + want;
            if (end > f->node->cap) {
                uint32_t nc = (f->node->cap != 0) ? f->node->cap : 64u;
                uint8_t *nd;
                while (nc < end) {
                    nc *= 2u;
                }
                if (nc > FS_MAX_FILE) {
                    nc = FS_MAX_FILE;
                }
                if (end > nc) {
                    return (total != 0) ? (int32_t)total
                                       : -NOVA_ENOSPC;
                }
                nd = (uint8_t *)kmalloc(nc);
                if (nd == 0) {
                    return (total != 0) ? (int32_t)total
                                       : -NOVA_ENOSPC;
                }
                for (uint32_t i = 0; i < f->node->size; i++) {
                    nd[i] = f->node->data[i];
                }
                if (f->node->data != 0) {
                    kfree(f->node->data);
                }
                f->node->data = nd;
                f->node->cap = nc;
            }
            if (fs_is_nova_path(f->store_path)) {
                rc = nova_write_at(f->store_path, f->off, kbuf, want);
            } else {
                rc = fat_write_at(f->store_path, f->off, kbuf, want);
            }
            if (rc != 0) {
                return (total != 0) ? (int32_t)total : rc;
            }
            for (uint32_t i = 0; i < want; i++) {
                f->node->data[f->off + i] = kbuf[i];
            }
            if (end > f->node->size) {
                f->node->size = end;
            }
            f->off = end;
            total += want;
        }
        return (int32_t)total;
    }
    while (total < len) {
        uint32_t want = len - total;
        uint32_t got = 0;
        int rc;
        if (want > sizeof(kbuf)) {
            want = sizeof(kbuf);
        }
        rc = copy_from_user(kbuf, buf + total, want);
        if (rc != 0) {
            return (total != 0) ? (int32_t)total : rc;
        }
        rc = fs_write_node(f->node, f->off, kbuf, want, &got);
        if (rc != 0) {
            return (total != 0) ? (int32_t)total : rc;
        }
        f->off += got;
        total += got;
        if (got < want) {
            break;
        }
    }
    return (int32_t)total;
}

static int32_t do_close(uint32_t fd, uint32_t b, uint32_t c) {
    struct process *p;
    (void)b;
    (void)c;
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    return fs_fd_drop(p, fd);
}

/* Copy a NUL-terminated kernel name (+NUL) to the user buffer. */
static int32_t copy_name_out(uint32_t namebuf, const char *kname, int r) {
    int rc = copy_to_user(namebuf, kname, (uint32_t)r + 1u);
    return (rc != 0) ? rc : r;
}

static int32_t do_readdir(uint32_t path, uint32_t index, uint32_t namebuf) {
    char kpath[FS_MAX_PATH + 1u];
    char name[FS_MAX_NAME];
    struct fs_node *dir = 0;
    struct process *p;
    int32_t plen;
    int rc;
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    if (validate_usermem(namebuf, FS_MAX_NAME, 1) != 0) {
        return -NOVA_EFAULT;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    /* /disk graft: FAT listing. /ext graft: EXT4 listing.
     * /nova graft: NOVA-FS listing. Jailed callers skip grafts
     * (Phase 10d): the ramfs tail below finds no graft dirs. */
    if (p->jail_root == 0 && fs_is_disk_path(kpath)) {
        char kname[FS_MAX_NAME];
        int r = fat_list_dir(kpath, index, kname);
        if (r < 0) {
            return r;
        }
        return copy_name_out(namebuf, kname, r);
    }
    if (p->jail_root == 0 && fs_is_ext_path(kpath)) {
        char kname[FS_MAX_NAME];
        int r = ext_list_dir(kpath, index, kname);
        if (r < 0) {
            return r;
        }
        return copy_name_out(namebuf, kname, r);
    }
    if (p->jail_root == 0 && fs_is_nova_path(kpath)) {
        char kname[FS_MAX_NAME];
        int r = nova_list_dir(kpath, index, kname);
        if (r < 0) {
            return r;
        }
        return copy_name_out(namebuf, kname, r);
    }
    rc = fs_lookup(kpath, &dir);
    if (rc != 0) {
        return rc;
    }
    /* Listing needs read on the dir itself (Phase 10c). Graft
     * listings enumerate foreign namespaces (opener-owned views);
     * ramfs is the permissioned one. */
    rc = fs_can(dir, fs_current_uid(), fs_current_gid(),
                FS_MAY_READ);
    if (rc != 0) {
        return rc;
    }
    rc = fs_readdir(dir, index, name);
    if (rc < 0) {
        return rc;
    }
    return copy_name_out(namebuf, name, rc);
}

static int32_t do_mkdir(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    struct process *p;
    int32_t plen;
    (void)b;
    (void)c;
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    if (p->jail_root == 0 && fs_is_disk_path(kpath)) {
        return fat_mkdir(kpath);
    }
    if (p->jail_root == 0 && fs_is_ext_path(kpath)) {
        return -NOVA_EROFS;
    }
    if (p->jail_root == 0 && fs_is_nova_path(kpath)) {
        return nova_mkdir(kpath);
    }
    return fs_mkdir(kpath);
}

/* Capability gate (Phase 10a): the calling thread's process must
 * hold the right. No current process (early boot, kernel context)
 * allows: gates only bind threads that exist. */
static int need_cap(uint32_t need) {
    struct thread *cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return NOVA_ESUCCESS;
    }
    return caps_allow(cur->proc->caps, need);
}

static int32_t do_exec(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)b;
    (void)c;
    /* Gate first (Phase 10a): no path work for the unprivileged. */
    if (need_cap(CAP_SPAWN) != 0) {
        return -NOVA_EPERM;
    }
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    /* Spawn semantics (no image replacement yet): a new process runs
     * the file's bytes; the caller keeps running. Returns child pid. */
    return spawn_file(kpath);
}

static int32_t do_fsck(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    /* Verify-only audit of the native FS (Phase 7f-3). No args, no
     * user pointers: returns the problem count (0 = clean). */
    return (int32_t)nova_fsck();
}

static int32_t do_ping(uint32_t addr, uint32_t seq, uint32_t c) {
    (void)c;
    /* Gate first (Phase 10a): network transmit needs CAP_NET. */
    if (need_cap(CAP_NET) != 0) {
        return -NOVA_EPERM;
    }
    /* Synchronous loopback ping (Phase 9a): RTT ticks or -errno
     * (EHOSTUNREACH off-loopback). Real NICs make this async. */
    return net_ping(addr, (uint16_t)(seq & 0xFFFFu));
}

static int32_t do_dhcp(uint32_t a, uint32_t b, uint32_t c) {
    (void)a;
    (void)b;
    (void)c;
    /* Gate first (Phase 10a): reconfiguring the network needs
     * CAP_NET, like transmitting on it. */
    if (need_cap(CAP_NET) != 0) {
        return -NOVA_EPERM;
    }
    /* Re-run DHCP acquisition (Phase 9c): 0 or -errno (ENODEV
     * without NIC, ETIMEDOUT on silence). No args, no user
     * pointers; the calling thread blocks for up to ~16s. */
    return (int32_t)dhcp_acquire();
}

static int32_t do_netinfo(uint32_t self_ptr, uint32_t gw_ptr,
                          uint32_t mask_ptr) {
    uint32_t *s;
    uint32_t *g;
    uint32_t *m;
    /* Current runtime IP config (Phase 9c): three validated u32
     * writes, mirroring do_meminfo. */
    if (validate_usermem(self_ptr, 4, 1) != 0 ||
        validate_usermem(gw_ptr, 4, 1) != 0 ||
        validate_usermem(mask_ptr, 4, 1) != 0) {
        return -NOVA_EFAULT;
    }
    s = (uint32_t *)self_ptr;
    g = (uint32_t *)gw_ptr;
    m = (uint32_t *)mask_ptr;
    *s = net_self_ip();
    *g = net_gw_ip();
    *m = net_netmask();
    return NOVA_ESUCCESS;
}

static int32_t do_caps(uint32_t op, uint32_t mask, uint32_t c) {
    struct thread *cur;
    struct process *p;
    uint32_t out;
    (void)c;
    /* Observe or shrink your own mask (Phase 10a): drops are
     * monotonic (caps_drop refuses unknown bits); no interface
     * raises caps, so sandboxes only tighten. */
    cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return -NOVA_EINVAL;
    }
    p = cur->proc;
    if (op == CAP_OP_GET) {
        return (int32_t)p->caps;
    }
    if (op == CAP_OP_DROP) {
        if (caps_drop(p->caps, mask, &out) != 0) {
            return -NOVA_EINVAL;
        }
        p->caps = out;
        return (int32_t)out;
    }
    return -NOVA_EINVAL;
}

static int32_t do_rlimit(uint32_t op, uint32_t which, uint32_t value) {
    struct thread *cur;
    struct process *p;
    uint32_t *field;
    uint32_t hard;
    uint32_t out;
    /* Observe or lower your own ceilings (Phase 10b): raising is
     * privilege gain (EPERM); unknown resources refuse (EINVAL).
     * Lowering below current use is allowed: only future
     * allocations feel it (fds, writes), and future spawns inherit
     * the tightened frame ceiling. */
    cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return -NOVA_EINVAL;
    }
    p = cur->proc;
    if (which == RLIM_NOFD) {
        field = &p->rlimit_nofd;
        hard = RLIM_NOFD_HARD;
    } else if (which == RLIM_FSIZE) {
        field = &p->rlimit_fsize;
        hard = RLIM_FSIZE_HARD;
    } else if (which == RLIM_FRAMES) {
        field = &p->rlimit_frames;
        hard = RLIM_FRAMES_HARD;
    } else if (which == RLIM_TICKS) {
        field = &p->rlimit_ticks;
        hard = RLIM_TICKS_HARD;
    } else {
        return -NOVA_EINVAL;
    }
    if (op == RLIM_OP_GET) {
        return (int32_t)*field;
    }
    if (op == RLIM_OP_SET) {
        int r = rlimit_set_rule(*field, value, hard, &out);
        if (r != 0) {
            return r;
        }
        *field = out;
        return (int32_t)out;
    }
    return -NOVA_EINVAL;
}

static int32_t do_cred(uint32_t op, uint32_t id, uint32_t c) {
    struct thread *cur;
    struct process *p;
    (void)c;
    /* Identity queries + voluntary downgrade (Phase 10c): root may
     * become anyone; anyone may reassert self (no-op success);
     * anything else is privilege gain (EPERM). Gid has no setter
     * in v0.1 (it follows at spawn). */
    cur = sched_current_thread();
    if (cur == 0 || cur->proc == 0) {
        return -NOVA_EINVAL;
    }
    p = cur->proc;
    if (op == CRED_OP_GETUID) {
        return (int32_t)p->uid;
    }
    if (op == CRED_OP_GETGID) {
        return (int32_t)p->gid;
    }
    if (op == CRED_OP_SETUID) {
        if (p->uid != UID_ROOT && id != p->uid) {
            return -NOVA_EPERM;
        }
        p->uid = id;
        return (int32_t)id;
    }
    return -NOVA_EINVAL;
}

static int32_t do_chmod(uint32_t path, uint32_t mode, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)c;
    /* Perm bits only (0777): setuid/setgid/sticky have no meaning
     * yet, so stray high bits refuse rather than silently drop. */
    if ((mode & ~0777u) != 0) {
        return -NOVA_EINVAL;
    }
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    /* Owner-or-root enforced inside fs_chmod (EACCES/EROFS flow). */
    return fs_chmod(kpath, (uint16_t)mode);
}

static int32_t do_unlink(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)b;
    (void)c;
    /* Delete a name (Phase 13a): ramfs unlinks (refcounted, open
     * descriptions survive); FAT/NOVA-FS delete through their
     * drivers; EXT4 refuses (read-only). Parent-W enforced in
     * the ramfs path, fail-closed like create. */
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    if (fs_is_disk_path(kpath)) {
        return fat_delete(kpath);
    }
    if (fs_is_ext_path(kpath)) {
        return -NOVA_EROFS;
    }
    if (fs_is_nova_path(kpath)) {
        return nova_delete(kpath);
    }
    return fs_unlink(kpath);
}

static int32_t do_rmdir(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)b;
    (void)c;
    /* Remove an empty dir (Phase 13a): ramfs only (no disk driver
     * removes directories yet). EBUSY for held roots, ENOTEMPTY
     * otherwise. Jailless callers see grafts as EROFS here. */
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    if (fs_is_disk_path(kpath) || fs_is_ext_path(kpath) ||
        fs_is_nova_path(kpath)) {
        return -NOVA_EROFS;
    }
    return fs_rmdir(kpath);
}

/* UDP sockets multiplex (Phase 14a): OPEN/CLOSE are bookkeeping;
 * SEND/RECV touch the network and need CAP_NET (gate-first, like
 * ping). Payloads cross via validated structs (SEND) or a static
 * bounce (RECV, 512B stack cap respected). */
static int32_t do_sock(uint32_t op, uint32_t a, uint32_t b) {
    if (op == SOCK_OP_OPEN) {
        if (need_cap(CAP_NET) != 0) {
            return -NOVA_EPERM;
        }
        if (a > 0xFFFFu) {
            return -NOVA_EINVAL;
        }
        return sock_open((uint16_t)a);
    }
    if (op == SOCK_OP_CLOSE) {
        return sock_close(a);
    }
    if (op == SOCK_OP_SEND) {
        struct nova_sock_send k;
        const struct nova_sock_send *u;
        /* Stack bounce (per-thread: the old static buffer let two
         * concurrent senders interleave payloads; ~1.5KB of the
         * 8KB kstack, callees below use statics). */
        uint8_t sbounce[1480];
        int rc;
        if (need_cap(CAP_NET) != 0) {
            return -NOVA_EPERM;
        }
        if (validate_usermem(b, sizeof(k), 0) != 0) {
            return -NOVA_EFAULT;
        }
        u = (const struct nova_sock_send *)b;
        k.dst = u->dst;
        k.dport = u->dport;
        k.buf = u->buf;
        k.len = u->len;
        /* Bounce the payload in (never hand user memory to the
         * stack: length checked first, so the copy is bounded). */
        if (k.len > SOCK_MTU_PAYLOAD) {
            return -NOVA_EINVAL;
        }
        rc = copy_from_user(sbounce, k.buf, k.len);
        if (rc != 0) {
            return rc;
        }
        return sock_send(a, k.dst, k.dport, sbounce, k.len);
    }
    if (op == SOCK_OP_RECV) {
        struct nova_sock_recv k;
        const struct nova_sock_recv *u;
        uint8_t bounce[1536];
        uint32_t got = 0;
        int rc;
        if (need_cap(CAP_NET) != 0) {
            return -NOVA_EPERM;
        }
        if (validate_usermem(b, sizeof(k), 0) != 0) {
            return -NOVA_EFAULT;
        }
        u = (const struct nova_sock_recv *)b;
        k.buf = u->buf;
        k.max = u->max;
        k.timeout = u->timeout;
        if (k.max > sizeof(bounce)) {
            return -NOVA_EINVAL;
        }
        if (k.max != 0 &&
            validate_usermem(k.buf, k.max, 1) != 0) {
            return -NOVA_EFAULT;
        }
        rc = sock_recv(a, bounce, k.max, k.timeout, &got);
        if (rc < 0) {
            return rc;
        }
        rc = copy_to_user(k.buf, bounce, got);
        if (rc != 0) {
            return rc;
        }
        return (int32_t)got;
    }
    return -NOVA_EINVAL;
}

static int32_t do_dns(uint32_t name, uint32_t ip_out, uint32_t c) {
    char kname[FS_MAX_PATH + 1u];
    uint32_t ip = 0;
    int32_t plen;
    int rc;
    (void)c;
    /* Resolve via the NAT proxy (Phase 14b): name grammar checked
     * inside dns_resolve; ETIMEDOUT on silence. */
    plen = copy_user_path(name, kname);
    if (plen < 0) {
        return plen;
    }
    if (validate_usermem(ip_out, 4, 1) != 0) {
        return -NOVA_EFAULT;
    }
    rc = dns_resolve(kname, &ip);
    if (rc != 0) {
        return rc;
    }
    *(volatile uint32_t *)ip_out = ip;
    return NOVA_ESUCCESS;
}

/* Jail assignment with lifetime (Phase 13a): the new root is
 * held first (self-assign is safe), the old hold dropped after.
 * Shared by do_chroot and the jail selftest (identical rules). */
static void proc_set_jail(struct process *p, struct fs_node *n) {
    if (p == 0) {
        return;
    }
    fs_node_ref(n);
    if (p->jail_root != 0) {
        fs_node_unref(p->jail_root);
    }
    p->jail_root = n;
}

static int32_t do_chroot(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    struct process *p;
    struct fs_node *n = 0;
    int32_t plen;
    int rc;
    (void)b;
    (void)c;
    /* Self-restriction (Phase 10d): anyone may narrow their own
     * view (never widen: the target resolves inside the current
     * namespace, so nested jails only shrink). Directories only. */
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    p = sysc_proc();
    if (p == 0) {
        return -NOVA_EINVAL;
    }
    rc = fs_lookup(kpath, &n);
    if (rc != 0) {
        return rc;
    }
    if (!n->is_dir) {
        return -NOVA_ENOTDIR;
    }
    proc_set_jail(p, n);
    return NOVA_ESUCCESS;
}

/* Window management (Phase 12d): ungated in v0.1 (any local
 * process arranges the shared console screen; per-window
 * ownership + a GUI capability are future). All user memory
 * validated like every other call. */
static int32_t do_winfocus(uint32_t a, uint32_t b, uint32_t c) {
    int id;
    (void)a;
    (void)b;
    (void)c;
    /* Cycle + repaint (the visible change); -1 when empty. */
    id = comp_focus_next();
    comp_present();
    return id;
}

static int32_t do_winopen(uint32_t args, uint32_t b, uint32_t c) {
    struct nova_winopen k;
    const struct nova_winopen *u;
    (void)b;
    (void)c;
    if (validate_usermem(args, sizeof(k), 0) != 0) {
        return -NOVA_EFAULT;
    }
    u = (const struct nova_winopen *)args;
    k.x = u->x;
    k.y = u->y;
    k.w = u->w;
    k.h = u->h;
    k.color = u->color;
    /* Geometry validated inside comp_open (EINVAL flows). */
    return comp_open(k.x, k.y, k.w, k.h, k.color);
}

static int32_t do_winstat(uint32_t buf, uint32_t max, uint32_t c) {
    uint32_t nbytes;
    (void)c;
    /* max entries bound the copy (93*44 = 4092 <= 4096 range
     * cap); count-only probe when max is 0. */
    if (max > 93u) {
        return -NOVA_EINVAL;
    }
    nbytes = max * (uint32_t)sizeof(struct nova_winstat);
    if (max != 0 && validate_usermem(buf, nbytes, 1) != 0) {
        return -NOVA_EFAULT;
    }
    return comp_stat(max == 0 ? 0 : (struct nova_winstat *)buf,
                     max);
}

static int32_t do_wintitle(uint32_t id, uint32_t str, uint32_t c) {
    char kbuf[16];
    int rc;
    (void)c;
    rc = copy_from_user(kbuf, str, sizeof(kbuf));
    if (rc != 0) {
        return rc;
    }
    kbuf[sizeof(kbuf) - 1u] = 0; /* truncation documented */
    return comp_title(id, kbuf);
}

static int32_t do_winclose(uint32_t id, uint32_t b, uint32_t c) {
    int rc;
    (void)b;
    (void)c;
    rc = comp_close(id);
    if (rc == 0) {
        comp_present();
    }
    return rc;
}

/* Click from user space (shell scripts, future apps): raw
 * screen coords, no validation needed (comp_hit compares,
 * paint clips). Returns the hit id, or -1 on miss. */
static int32_t do_winclick(uint32_t x, uint32_t y, uint32_t c) {
    (void)c;
    return comp_click(x, y);
}

/* Read-only kernel counters (Phase 0.3-G observability):
 * uptime, completed switches, handled IRQs, live threads,
 * free PMM frames. Snapshot into a local struct, one copy
 * out (never expose kernel addresses; counters only). */
static int32_t do_stats(uint32_t out, uint32_t b, uint32_t c) {
    struct nova_stats k;
    (void)b;
    (void)c;
    k.ticks = g_ticks;
    k.switches = g_sched_switches;
    k.irqs = g_irq_count;
    k.threads = thread_count();
    k.pmm_free = pmm_free_count();
    return copy_to_user(out, &k, sizeof(k));
}

/* Bounded trace snapshot (Phase C, ADR-0077 design C+E).
 * World-readable by decision (all analogous counters are;
 * projection — not scope — is the control; a capability
 * gate is a 3-line change if policy ever requires it).
 * Semantics: count==0 returns 0 touching nothing (not even
 * validation); max > TRACEFETCH_MAX refuses EINVAL; empty
 * ring + valid buffer returns 0 (short-read rule below);
 * empty ring + bad buffer still refuses EFAULT (the validated
 * range max*20 is nonempty even though nothing is held);
 * short rings return what's held (newest-N window,
 * oldest-first); misses of all kinds return -errno, never
 * partial garbage. IF discipline: save/restore (never blind
 * sti: callers may hold IF=0). */
static struct nova_trace_rec trace_snap[TRACEFETCH_MAX];

static int32_t do_tracefetch(uint32_t buf, uint32_t max, uint32_t c) {
    uint32_t n, take, i, irqf;
    (void)c;
    if (max == 0) {
        return 0;
    }
    if (max > TRACEFETCH_MAX) {
        return -NOVA_EINVAL;
    }
    /* Exact byte range (max*20 <= 5120: no overflow possible
     * past the cap above; validate_usermem re-checks anyway). */
    if (validate_usermem(buf, max * (uint32_t)sizeof(trace_snap[0]),
                          1) != 0) {
        return -NOVA_EFAULT;
    }
    /* Snapshot under IF (save/restore, never blind cli/sti:
     * callers may hold IF=0 across the call — boot selftests
     * and benches do, with foreign tables loaded. The copy
     * below runs with the caller's IF. */
    irqf = arch_irq_save();
    n = trace_count();
    take = (n < max) ? n : max;
    for (i = 0; i < take; i++) {
        struct trace_event e;
        /* Cannot fail: IF-held, indices pre-validated against
         * the count just read. */
        trace_read(n - take + i, &e);
        trace_project(&e, &trace_snap[i]);
    }
    arch_irq_restore(irqf);
    {
        int rc = copy_to_user(buf, trace_snap,
                              take * (uint32_t)sizeof(trace_snap[0]));
        if (rc != 0) {
            return rc;
        }
    }
    return (int32_t)take;
}

/* Per-thread runtime query (Phase F): tid in, snapshot out.
 * tid==0 means the caller. Snapshot via sched_thread_stat
 * (scalars only, never pointers); unknown/exited threads
 * refuse ENOENT (graveyard not searched: dead means gone).
 * World-readable (stats precedent; counters only). */
static int32_t do_runtime(uint32_t tid, uint32_t out, uint32_t c) {
    struct nova_runtime k;
    uint32_t id = tid;
    uint32_t rt;
    int st;
    uint32_t pr;
    (void)c;
    if (id == 0) {
        struct thread *me = sched_current_thread();
        if (me == 0) {
            return -NOVA_ENOENT;
        }
        id = me->id;
    }
    if (!sched_thread_stat(id, &rt, &st, &pr)) {
        return -NOVA_ENOENT;
    }
    k.tid = id;
    k.runtime = rt;
    k.state = (uint32_t)st;
    k.prio = pr;
    return copy_to_user(out, &k, sizeof(k));
}

typedef int32_t (*sys_fn_t)(uint32_t, uint32_t, uint32_t);

static const struct {
    uint32_t nr;
    const char *name;
    sys_fn_t fn;
} sys_table[] = {
    { SYS_YIELD, "yield", do_yield },
    { SYS_EXIT, "exit", do_exit },
    { SYS_PRINT, "print", do_print },
    { SYS_GETPID, "getpid", do_getpid },
    { SYS_GETTID, "gettid", do_gettid },
    { SYS_VERSION, "version", do_version },
    { SYS_GETKEY, "getkey", do_getkey },
    { SYS_MEMINFO, "meminfo", do_meminfo },
    { SYS_TICKS, "ticks", do_ticks },
    { SYS_OPEN, "open", do_open },
    { SYS_READ, "read", do_read },
    { SYS_WRITE, "write", do_write },
    { SYS_CLOSE, "close", do_close },
    { SYS_READDIR, "readdir", do_readdir },
    { SYS_MKDIR, "mkdir", do_mkdir },
    { SYS_EXEC, "exec", do_exec },
    { SYS_FSCK, "fsck", do_fsck },
    { SYS_PING, "ping", do_ping },
    { SYS_DHCP, "dhcp", do_dhcp },
    { SYS_NETINFO, "netinfo", do_netinfo },
    { SYS_CAPS, "caps", do_caps },
    { SYS_RLIMIT, "rlimit", do_rlimit },
    { SYS_CRED, "cred", do_cred },
    { SYS_CHMOD, "chmod", do_chmod },
    { SYS_CHROOT, "chroot", do_chroot },
    { SYS_UNLINK, "unlink", do_unlink },
    { SYS_RMDIR, "rmdir", do_rmdir },
    { SYS_SOCK, "sock", do_sock },
    { SYS_DNS, "dns", do_dns },
    { SYS_FOCUS, "winfocus", do_winfocus },
    { SYS_WINOPEN, "winopen", do_winopen },
    { SYS_WINSTAT, "winstat", do_winstat },
    { SYS_WINTITLE, "wintitle", do_wintitle },
    { SYS_WINCLOSE, "winclose", do_winclose },
    { SYS_WINCLICK, "winclick", do_winclick },
    { SYS_STATS, "stats", do_stats },
    { SYS_TRACEFETCH, "tracefetch", do_tracefetch },
    { SYS_RUNTIME, "runtime", do_runtime },
};

void syscall_handler(struct syscall_frame *f) {
    uint32_t nr = f->eax;
    for (uint32_t i = 0;
         i < sizeof(sys_table) / sizeof(sys_table[0]); i++) {
        if (sys_table[i].nr == nr) {
            f->eax = (uint32_t)sys_table[i].fn(f->ebx, f->ecx, f->edx);
            return;
        }
    }
    f->eax = (uint32_t)(-NOVA_ENOSYS);
}

void syscall_init(void) {
    idt_set_gate(0x80, syscall_stub, 0xEEu);
    /* Permanent hygiene: the gate must resolve to the stub. */
    {
        arch_idtr_t idtr;
        uint32_t *gate;
        arch_store_idt(&idtr);
        gate = (uint32_t *)(idtr.base + 0x80u * 8u);
        if ((gate[0] & 0xFFFFu) != ((uint32_t)syscall_stub & 0xFFFFu) ||
            (gate[1] >> 16) != ((uint32_t)syscall_stub >> 16) ||
            ((gate[1] >> 8) & 0xFFu) != 0xEEu) {
            nova_panic("syscall-gate-fail");
        }
    }
}

/* --- selftest: the sandbox demonstrably removes rights (Phase 10a,
 * ADR-0007 testability rule). A capless scratch process attempts
 * gated calls through the real dispatch (crafted frames, current
 * thread swapped under cli so no tick can observe the fake). --- */
int caps_selftest(void) {
    struct process *victim;
    struct thread fake;
    struct thread *saved;
    struct syscall_frame f;
    victim = process_create();
    if (victim == 0) {
        serial_puts("CAPS-FAIL alloc\n");
        return -1;
    }
    if (victim->caps != 0 || victim->uid != UID_NOBODY) {
        serial_puts("CAPS-FAIL default-deny\n");
        return -1;
    }
    fake.sp = 0;
    fake.id = 0xFFFFFFFEu;
    fake.state = THREAD_RUNNING;
    fake.stack_base = 0;
    fake.slice_used = 0;
    fake.prio = 0;
    fake.is_user = 0;
    fake.next = 0;
    fake.pnext = 0;
    fake.proc = victim;
    fake.user_stack = 0;
    fake.user_shared = 0;
    f.kflags = 0;
    f.edi = 0;
    f.esi = 0;
    f.ebp = 0;
    f.esp_ignored = 0;
    f.ebx = 0;
    f.edx = 0;
    f.ecx = 0;
    f.eax = 0;
    f.eip = 0;
    f.cs = 0;
    f.eflags = 0;
    f.esp = 0;
    f.ss = 0;
    saved = sched_current_thread();
    /* Failure discipline: any CAPS-FAIL below returns with the CPU
     * still masked and current still fake -- safe because pm_main
     * panics immediately (no tick can observe the fake, halt wants
     * interrupts off anyway). Success restores both. */
    __asm__ volatile("cli" ::: "memory");
    sched_enter(&fake);
    /* Capless: ping/dhcp/exec all refuse (exec refuses before
     * touching the bogus path: gates run first). */
    f.eax = SYS_PING;
    f.ebx = 0x7F000001u;
    f.ecx = 1;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("CAPS-FAIL ping-open\n");
        return -1;
    }
    f.eax = SYS_DHCP;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("CAPS-FAIL dhcp-open\n");
        return -1;
    }
    f.eax = SYS_EXEC;
    f.ebx = 0;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("CAPS-FAIL exec-open\n");
        return -1;
    }
    /* Observe + bad-drop discipline. */
    f.eax = SYS_CAPS;
    f.ebx = CAP_OP_GET;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("CAPS-FAIL get\n");
        return -1;
    }
    f.eax = SYS_CAPS;
    f.ebx = CAP_OP_DROP;
    f.ecx = 0x04u; /* unknown bit: must not silently pass */
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL) {
        serial_puts("CAPS-FAIL badbit\n");
        return -1;
    }
    /* Grant all: the same calls now succeed (loopback ping proves
     * the positive path end to end). */
    victim->caps = CAPS_ALL;
    f.eax = SYS_PING;
    f.ebx = 0x7F000001u;
    f.ecx = 9;
    syscall_handler(&f);
    if ((int32_t)f.eax < 0) {
        serial_puts("CAPS-FAIL ping-shut\n");
        return -1;
    }
    /* Drop NET: monotonic (get confirms the smaller mask; ping and
     * dhcp refuse again; spawn still allowed). */
    f.eax = SYS_CAPS;
    f.ebx = CAP_OP_DROP;
    f.ecx = CAP_NET;
    syscall_handler(&f);
    if ((int32_t)f.eax != (int32_t)(CAPS_ALL & ~CAP_NET)) {
        serial_puts("CAPS-FAIL drop\n");
        return -1;
    }
    f.eax = SYS_PING;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("CAPS-FAIL ping-relock\n");
        return -1;
    }
    f.eax = SYS_DHCP;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("CAPS-FAIL dhcp-relock\n");
        return -1;
    }
    __asm__ volatile("sti" ::: "memory");
    sched_enter(saved);
    kfree(victim);
    serial_puts("CAPS-OK\n");
    return NOVA_ESUCCESS;
}

/* --- selftest: ceilings demonstrably bind (Phase 10b). A scratch
 * process exercises SYS_RLIMIT (get/lower/raise-refuse/bad-which),
 * the fd ceiling (alloc/EMFILE under a lowered cap), the fsize
 * clamp (vectors), and the frame ceiling (map/map/refuse on a
 * capped address space). Prints RLIM-OK. --- */
int rlim_selftest(void) {
    struct process *tp;
    struct thread fake;
    struct thread *saved;
    struct syscall_frame f;
    struct fs_file fa, fb, fc;
    struct addrspace *as;
    uint32_t f1, f2;
    uint32_t oklen;
    int d0, d1;
    tp = process_create();
    if (tp == 0) {
        serial_puts("RLIM-FAIL alloc\n");
        return -1;
    }
    if (tp->rlimit_nofd != RLIM_NOFD_DEF ||
        tp->rlimit_fsize != RLIM_FSIZE_DEF ||
        tp->rlimit_frames != RLIM_FRAMES_DEF ||
        tp->rlimit_ticks != RLIM_TICKS_DEF || tp->ticks_used != 0) {
        serial_puts("RLIM-FAIL defaults\n");
        return -1;
    }
    /* Direct fd proof (no scheduler needed: alloc takes the proc).
     * Stack files are never dropped (drop frees: stack must not
     * reach kfree); slots are cleared by hand at the end. */
    d0 = fs_fd_alloc(tp, &fa);
    d1 = fs_fd_alloc(tp, &fb);
    if (d0 != 0 || d1 != 1) {
        serial_puts("RLIM-FAIL fd-open\n");
        return -1;
    }
    fake.sp = 0;
    fake.id = 0xFFFFFFFDu;
    fake.state = THREAD_RUNNING;
    fake.stack_base = 0;
    fake.slice_used = 0;
    fake.prio = 0;
    fake.is_user = 0;
    fake.next = 0;
    fake.pnext = 0;
    fake.proc = tp;
    fake.user_stack = 0;
    fake.user_shared = 0;
    f.kflags = 0;
    f.edi = 0;
    f.esi = 0;
    f.ebp = 0;
    f.esp_ignored = 0;
    f.ebx = 0;
    f.edx = 0;
    f.ecx = 0;
    f.eax = 0;
    f.eip = 0;
    f.cs = 0;
    f.eflags = 0;
    f.esp = 0;
    f.ss = 0;
    saved = sched_current_thread();
    /* Same failure discipline as caps_selftest: returns below run
     * masked with a borrowed current; pm_main panics at once. */
    __asm__ volatile("cli" ::: "memory");
    sched_enter(&fake);
    /* Observe defaults through dispatch. */
    f.eax = SYS_RLIMIT;
    f.ebx = RLIM_OP_GET;
    f.ecx = RLIM_NOFD;
    syscall_handler(&f);
    if ((int32_t)f.eax != (int32_t)RLIM_NOFD_DEF) {
        serial_puts("RLIM-FAIL get-nofd\n");
        return -1;
    }
    /* Lower nofd to 2 (2 already open): allowed, binds at once. */
    f.eax = SYS_RLIMIT;
    f.ebx = RLIM_OP_SET;
    f.ecx = RLIM_NOFD;
    f.edx = 2;
    syscall_handler(&f);
    if ((int32_t)f.eax != 2) {
        serial_puts("RLIM-FAIL set-nofd\n");
        return -1;
    }
    if (fs_fd_alloc(tp, &fc) != -NOVA_EMFILE) {
        serial_puts("RLIM-FAIL fd-ceiling\n");
        return -1;
    }
    /* Raising refuses (privilege); over-hard refuses (range);
     * unknown resource refuses. */
    f.eax = SYS_RLIMIT;
    f.ebx = RLIM_OP_SET;
    f.ecx = RLIM_NOFD;
    f.edx = 3;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("RLIM-FAIL raise\n");
        return -1;
    }
    f.eax = SYS_RLIMIT;
    f.ebx = RLIM_OP_SET;
    f.ecx = RLIM_NOFD;
    f.edx = NOVA_NFD + 1u;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL) {
        serial_puts("RLIM-FAIL overhard\n");
        return -1;
    }
    f.eax = SYS_RLIMIT;
    f.ebx = RLIM_OP_SET;
    f.ecx = 9u;
    f.edx = 1;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL) {
        serial_puts("RLIM-FAIL badwhich\n");
        return -1;
    }
    /* fsize clamp vectors (the do_write gate shares this logic). */
    if (rlimit_fsize_allow(100, 0, 50, &oklen) != 0 ||
        oklen != 50) {
        serial_puts("RLIM-FAIL clamp-open\n");
        return -1;
    }
    if (rlimit_fsize_allow(100, 80, 50, &oklen) != 0 ||
        oklen != 20) {
        serial_puts("RLIM-FAIL clamp-trim\n");
        return -1;
    }
    if (rlimit_fsize_allow(100, 100, 10, &oklen) !=
        -NOVA_EFBIG) {
        serial_puts("RLIM-FAIL clamp-full\n");
        return -1;
    }
    if (rlimit_fsize_allow(100, 150, 10, &oklen) !=
        -NOVA_EFBIG) {
        serial_puts("RLIM-FAIL clamp-past\n");
        return -1;
    }
    /* Frame ceiling on a capped address space (map/map/refuse). */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("RLIM-FAIL as-alloc\n");
        return -1;
    }
    as->max_mapped = 2;
    if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
        addrspace_map(as, USER_BASE + 4096u, f2, 1) != 0) {
        serial_puts("RLIM-FAIL as-open\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE + 8192u, f1, 1) !=
        -NOVA_ENOSPC) {
        serial_puts("RLIM-FAIL as-ceiling\n");
        return -1;
    }
    addrspace_destroy(as);
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    tp->fds[d0] = 0;
    tp->fds[d1] = 0;
    __asm__ volatile("sti" ::: "memory");
    sched_enter(saved);
    kfree(tp);
    serial_puts("RLIM-OK\n");
    return NOVA_ESUCCESS;
}

/* --- selftest: ownership demonstrably binds (Phase 10c). A scratch
 * process (nobody) fails to create in the root-owned tree, then as
 * root seeds /perm/f, chmods it shut, and every access as nobody
 * refuses while triplets/root behave. Prints PERM-OK. Leaves
 * /perm + /perm/f behind (no unlink yet; namespaced, enumerated
 * by nothing later in boot). --- */
int perm_selftest(void) {
    struct process *tp;
    struct thread fake;
    struct thread *saved;
    struct syscall_frame f;
    struct fs_node *n = 0;
    tp = process_create();
    if (tp == 0) {
        serial_puts("PERM-FAIL alloc\n");
        return -1;
    }
    if (tp->gid != UID_NOBODY) {
        serial_puts("PERM-FAIL gid-default\n");
        return -1;
    }
    fake.sp = 0;
    fake.id = 0xFFFFFFFCu;
    fake.state = THREAD_RUNNING;
    fake.stack_base = 0;
    fake.slice_used = 0;
    fake.prio = 0;
    fake.is_user = 0;
    fake.next = 0;
    fake.pnext = 0;
    fake.proc = tp;
    fake.user_stack = 0;
    fake.user_shared = 0;
    f.kflags = 0;
    f.edi = 0;
    f.esi = 0;
    f.ebp = 0;
    f.esp_ignored = 0;
    f.ebx = 0;
    f.edx = 0;
    f.ecx = 0;
    f.eax = 0;
    f.eip = 0;
    f.cs = 0;
    f.eflags = 0;
    f.esp = 0;
    f.ss = 0;
    saved = sched_current_thread();
    /* Same masked-current discipline as caps/rlim selftests. */
    __asm__ volatile("cli" ::: "memory");
    sched_enter(&fake);
    /* Nobody cannot create in the root-owned tree (/ is 755:
     * other lacks W). Fail-closed, nothing revealed. */
    if (fs_mkdir("/perm") != -NOVA_EACCES) {
        serial_puts("PERM-FAIL mkdir-deny\n");
        return -1;
    }
    if (fs_create("/perm-f", &n) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL create-deny\n");
        return -1;
    }
    /* Harness privilege: become root, seed the fixture. */
    tp->uid = UID_ROOT;
    if (fs_mkdir("/perm") != 0) {
        serial_puts("PERM-FAIL mkdir-root\n");
        return -1;
    }
    if (fs_lookup("/perm", &n) != 0 || n->uid != UID_ROOT ||
        n->mode != 0755) {
        serial_puts("PERM-FAIL mkdir-owner\n");
        return -1;
    }
    if (fs_create("/perm/f", &n) != 0) {
        serial_puts("PERM-FAIL create-root\n");
        return -1;
    }
    if (fs_chmod("/perm/f", 0600) != 0) {
        serial_puts("PERM-FAIL chmod-root\n");
        return -1;
    }
    /* Back to nobody: chmod is owner-or-root only. */
    tp->uid = UID_NOBODY;
    if (fs_chmod("/perm/f", 0644) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL chmod-deny\n");
        return -1;
    }
    /* Native deny (Phase 13d): NOTE.TXT belongs to uid 1000 on
     * disk, so a stranger's chmod refuses at the inode. */
    if (fs_chmod("/nova/DOCS/NOTE.TXT", 0644) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL nova-deny\n");
        return -1;
    }
    /* Open gates bind (0600 root file vs nobody). */
    if (fs_open_check(tp, n, FS_O_RDONLY) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL open-r\n");
        return -1;
    }
    if (fs_open_check(tp, n, FS_O_WRONLY) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL open-w\n");
        return -1;
    }
    if (fs_open_check(tp, n, FS_O_RDWR) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL open-rw\n");
        return -1;
    }
    /* Lifecycle denials bind too (parent unwritable for nobody). */
    if (fs_unlink("/perm/f") != -NOVA_EACCES) {
        serial_puts("PERM-FAIL unlink-deny\n");
        return -1;
    }
    if (fs_rmdir("/perm") != -NOVA_EACCES) {
        serial_puts("PERM-FAIL rmdir-deny\n");
        return -1;
    }
    /* Root passes everything (bypass). */
    tp->uid = UID_ROOT;
    if (fs_open_check(tp, n, FS_O_RDONLY) != 0 ||
        fs_open_check(tp, n, FS_O_RDWR) != 0) {
        serial_puts("PERM-FAIL open-root\n");
        return -1;
    }
    /* Triplet discipline on 0640 (group = creator gid 65534). */
    if (fs_chmod("/perm/f", 0640) != 0) {
        serial_puts("PERM-FAIL chmod-group\n");
        return -1;
    }
    if (fs_can(n, 99, UID_NOBODY, FS_MAY_READ) != 0) {
        serial_puts("PERM-FAIL group-r\n");
        return -1;
    }
    if (fs_can(n, 99, UID_NOBODY, FS_MAY_WRITE) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL group-w\n");
        return -1;
    }
    if (fs_can(n, 99, 98, FS_MAY_READ) != -NOVA_EACCES) {
        serial_puts("PERM-FAIL other-r\n");
        return -1;
    }
    /* Graft chmod refuses EROFS (on-disk editing is future). */
    if (fs_chmod("/disk/x", 0644) != -NOVA_EROFS) {
        serial_puts("PERM-FAIL chmod-erofs\n");
        return -1;
    }
    /* Credential dispatch: observe, refused rise, allowed drop. */
    tp->uid = UID_NOBODY;
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_GETUID;
    syscall_handler(&f);
    if ((int32_t)f.eax != (int32_t)UID_NOBODY) {
        serial_puts("PERM-FAIL getuid\n");
        return -1;
    }
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_GETGID;
    syscall_handler(&f);
    if ((int32_t)f.eax != (int32_t)UID_NOBODY) {
        serial_puts("PERM-FAIL getgid\n");
        return -1;
    }
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_SETUID;
    f.ecx = UID_ROOT;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("PERM-FAIL setuid-rise\n");
        return -1;
    }
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_SETUID;
    f.ecx = 1000u;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EPERM) {
        serial_puts("PERM-FAIL setuid-side\n");
        return -1;
    }
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_SETUID;
    f.ecx = UID_NOBODY;
    syscall_handler(&f);
    if ((int32_t)f.eax != (int32_t)UID_NOBODY) {
        serial_puts("PERM-FAIL setuid-self\n");
        return -1;
    }
    tp->uid = UID_ROOT;
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_SETUID;
    f.ecx = 1000u;
    syscall_handler(&f);
    if ((int32_t)f.eax != 1000) {
        serial_puts("PERM-FAIL setuid-root\n");
        return -1;
    }
    f.eax = SYS_CRED;
    f.ebx = CRED_OP_GETUID;
    syscall_handler(&f);
    if ((int32_t)f.eax != 1000) {
        serial_puts("PERM-FAIL getuid-after\n");
        return -1;
    }
    __asm__ volatile("sti" ::: "memory");
    sched_enter(saved);
    kfree(tp);
    serial_puts("PERM-OK\n");
    return NOVA_ESUCCESS;
}

/* --- selftest: the jail demonstrably confines (Phase 10d). A
 * scratch process is rooted into a fresh /jail subtree: global
 * names vanish (ENOENT, not refused), ".." clamps at the floor,
 * nested jails shrink further, and only harness privilege (never
 * a syscall) restores the global view. Prints JAIL-OK. Leaves
 * /jail behind (no unlink yet; namespaced like /perm). --- */
int jail_selftest(void) {
    struct process *tp;
    struct thread fake;
    struct thread *saved;
    struct syscall_frame f;
    struct fs_node *n = 0;
    struct fs_node *jail = 0;
    struct fs_node *sub = 0;
    tp = process_create();
    if (tp == 0) {
        serial_puts("JAIL-FAIL alloc\n");
        return -1;
    }
    if (tp->jail_root != 0) {
        serial_puts("JAIL-FAIL jail-default\n");
        return -1;
    }
    fake.sp = 0;
    fake.id = 0xFFFFFFFBu;
    fake.state = THREAD_RUNNING;
    fake.stack_base = 0;
    fake.slice_used = 0;
    fake.prio = 0;
    fake.is_user = 0;
    fake.next = 0;
    fake.pnext = 0;
    fake.proc = tp;
    fake.user_stack = 0;
    fake.user_shared = 0;
    f.kflags = 0;
    f.edi = 0;
    f.esi = 0;
    f.ebp = 0;
    f.esp_ignored = 0;
    f.ebx = 0;
    f.edx = 0;
    f.ecx = 0;
    f.eax = 0;
    f.eip = 0;
    f.cs = 0;
    f.eflags = 0;
    f.esp = 0;
    f.ss = 0;
    /* Harness privilege for the fixture (seeds are root-owned). */
    tp->uid = UID_ROOT;
    saved = sched_current_thread();
    /* Same masked-current discipline as the other sandbox proofs. */
    __asm__ volatile("cli" ::: "memory");
    sched_enter(&fake);
    if (fs_mkdir("/jail") != 0 || fs_mkdir("/jail/sub") != 0 ||
        fs_create("/jail/sub/f", &n) != 0) {
        serial_puts("JAIL-FAIL seed\n");
        return -1;
    }
    if (fs_lookup("/jail", &jail) != 0 || jail == 0 ||
        fs_lookup("/jail/sub", &sub) != 0 || sub == 0) {
        serial_puts("JAIL-FAIL fixture\n");
        return -1;
    }
    /* Explicit-root seam (no current needed): scoping + floor. */
    if (fs_lookup_at(jail, "/sub/f", &n) != 0) {
        serial_puts("JAIL-FAIL at-open\n");
        return -1;
    }
    if (fs_lookup_at(jail, "/../hello.txt", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL at-escape\n");
        return -1;
    }
    if (fs_lookup_at(jail, "/disk/HELLO.TXT", &n) !=
        -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL at-graft\n");
        return -1;
    }
    /* Confine the process (the chroot effect, arranged directly:
     * SYS_CHROOT dispatch needs a user path, unreachable here). */
    proc_set_jail(tp, jail);
    if (fs_lookup("/sub/f", &n) != 0) {
        serial_puts("JAIL-FAIL in-open\n");
        return -1;
    }
    if (fs_lookup("/", &n) != 0 || n != jail) {
        serial_puts("JAIL-FAIL in-root\n");
        return -1;
    }
    if (fs_lookup("/../hello.txt", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL in-escape\n");
        return -1;
    }
    if (fs_lookup("/hello.txt", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL in-global\n");
        return -1;
    }
    if (fs_lookup("/disk/HELLO.TXT", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL in-graft\n");
        return -1;
    }
    if (fs_lookup("/bin/hi", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL in-spawn\n");
        return -1;
    }
    /* Nested: deeper floor, smaller world (".." clamps, siblings
     * vanish). */
    proc_set_jail(tp, sub);
    if (fs_lookup("/f", &n) != 0) {
        serial_puts("JAIL-FAIL nest-open\n");
        return -1;
    }
    if (fs_lookup("/..", &n) != 0 || n != sub) {
        serial_puts("JAIL-FAIL nest-floor\n");
        return -1;
    }
    if (fs_lookup("/sub", &n) != -NOVA_ENOENT) {
        serial_puts("JAIL-FAIL nest-scope\n");
        return -1;
    }
    /* A jailed-in dir cannot be removed from outside (held
     * root, EBUSY). Needs two actors: tp stays inside while a
     * second scratch process acts globally (a jail root is
     * unnameable from inside: every spelling is rejected before
     * reaching it, so confinement needs no EBUSY of its own). */
    {
        struct process *tp2 = process_create();
        struct thread fake2;
        if (tp2 == 0) {
            serial_puts("JAIL-FAIL rmdir-actor\n");
            return -1;
        }
        tp2->uid = UID_ROOT;
        fake2.sp = 0;
        fake2.id = 0xFFFFFFFAu;
        fake2.state = THREAD_RUNNING;
        fake2.stack_base = 0;
        fake2.slice_used = 0;
        fake2.prio = 0;
        fake2.is_user = 0;
        fake2.next = 0;
        fake2.pnext = 0;
        fake2.proc = tp2;
        fake2.user_stack = 0;
        fake2.user_shared = 0;
        /* Empty it first, addressed jail-relatively ("/f": the
         * global spelling is unresolvable from inside). */
        if (fs_unlink("/f") != 0) {
            serial_puts("JAIL-FAIL unlink-in-jail\n");
            return -1;
        }
        sched_enter(&fake2);
        if (fs_rmdir("/jail/sub") != -NOVA_EBUSY) {
            serial_puts("JAIL-FAIL rmdir-busy\n");
            return -1;
        }
        sched_enter(&fake);
        kfree(tp2);
    }
    /* Harness-only restore (no syscall unjails, by design). */
    proc_set_jail(tp, 0);
    if (fs_lookup("/hello.txt", &n) != 0) {
        serial_puts("JAIL-FAIL restore\n");
        return -1;
    }
    __asm__ volatile("sti" ::: "memory");
    sched_enter(saved);
    kfree(tp);
    serial_puts("JAIL-OK\n");
    return NOVA_ESUCCESS;
}

/* --- selftest: window dispatch binds (Phase 12d). No current
 * needed (handlers are proc-agnostic): crafted frames drive real
 * dispatch against the empty pool (comp_demo runs later). Empty
 * focus, bad pointers, bad ids, over-max stat refuse; a direct
 * open + dispatched focus + direct stat prove the positive path.
 * Prints WIN-OK. --- */
int win_selftest(void) {
    struct syscall_frame f;
    struct nova_winstat st[COMP_MAXWIN];
    int id;
    f.kflags = 0;
    f.edi = 0;
    f.esi = 0;
    f.ebp = 0;
    f.esp_ignored = 0;
    f.ebx = 0;
    f.edx = 0;
    f.ecx = 0;
    f.eax = 0;
    f.eip = 0;
    f.cs = 0;
    f.eflags = 0;
    f.esp = 0;
    f.ss = 0;
    /* Empty pool: focus finds nothing. */
    f.eax = SYS_FOCUS;
    syscall_handler(&f);
    if ((int32_t)f.eax != -1) {
        serial_puts("WIN-FAIL focus-empty\n");
        return -1;
    }
    /* Empty pool: click misses (no windows, silent -1). */
    f.eax = SYS_WINCLICK;
    f.ebx = 160;
    f.ecx = 100;
    syscall_handler(&f);
    if ((int32_t)f.eax != -1) {
        serial_puts("WIN-FAIL click-empty\n");
        return -1;
    }
    /* Bad user memory refuses before anything happens. */
    f.eax = SYS_WINOPEN;
    f.ebx = 0;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("WIN-FAIL open-null\n");
        return -1;
    }
    f.eax = SYS_WINTITLE;
    f.ebx = 0;
    f.ecx = 0;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("WIN-FAIL title-null\n");
        return -1;
    }
    f.eax = SYS_WINSTAT;
    f.ebx = 0;
    f.ecx = 94u;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL) {
        serial_puts("WIN-FAIL stat-max\n");
        return -1;
    }
    /* Count probe on empty pool (NULL,0 is legal). */
    f.eax = SYS_WINSTAT;
    f.ebx = 0;
    f.ecx = 0;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("WIN-FAIL stat-zero\n");
        return -1;
    }
    /* Unused id refuses (pool still empty). */
    f.eax = SYS_WINCLOSE;
    f.ebx = 7;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL) {
        serial_puts("WIN-FAIL close-empty\n");
        return -1;
    }
    /* Positive path: direct open, dispatched focus, direct stat
     * (user buffers are unreachable from here; the copy loop is
     * reviewed, the validation above executed). */
    id = comp_open(10, 150, 60, 40, FB_RED);
    if (id != 0) {
        serial_puts("WIN-FAIL open\n");
        return -1;
    }
    f.eax = SYS_FOCUS;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("WIN-FAIL focus\n");
        return -1;
    }
    if (comp_stat(st, COMP_MAXWIN) != 1 || st[0].id != 0 ||
        st[0].x != 10u || st[0].focused != 1u ||
        st[0].title[0] != 0) {
        serial_puts("WIN-FAIL stat\n");
        return -1;
    }
    /* Click through the syscall: inside (10,150,60,40) hits 0,
     * the bar zone misses. */
    f.eax = SYS_WINCLICK;
    f.ebx = 20;
    f.ecx = 160;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0 || comp_focused() != 0) {
        serial_puts("WIN-FAIL click-hit\n");
        return -1;
    }
    f.eax = SYS_WINCLICK;
    f.ebx = 20;
    f.ecx = 4;
    syscall_handler(&f);
    if ((int32_t)f.eax != -1 || comp_focused() != 0) {
        serial_puts("WIN-FAIL click-miss\n");
        return -1;
    }
    f.eax = SYS_WINCLOSE;
    f.ebx = 0;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("WIN-FAIL close\n");
        return -1;
    }
    if (comp_stat(0, 0) != 0) {
        serial_puts("WIN-FAIL drain\n");
        return -1;
    }
    serial_puts("WIN-OK\n");
    return NOVA_ESUCCESS;
}

/* Stats dispatch proof (SYS_STATS): NULL refuses (EFAULT), then a
 * real user page through real dispatch. The scratch addrspace
 * clones kernel-low (code/data visible), maps one US/RW frame,
 * and lends its CR3 for two dispatches under cli (no IRQ runs
 * foreign tables); restored + destroyed after. Deterministic. */
static int stats_probe_user(void) {
    struct syscall_frame f;
    struct addrspace *as;
    uint32_t frame;
    uint32_t saved;
    struct nova_stats *st;
    as = addrspace_create();
    frame = pmm_alloc_frame();
    if (as == 0 || frame == 0) {
        serial_puts("STATS-FAIL alloc\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, frame, 1) != 0) {
        serial_puts("STATS-FAIL map\n");
        return -1;
    }
    f.ebx = 0;
    f.ecx = 0;
    f.edx = 0;
    f.eax = SYS_STATS;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("STATS-FAIL null\n");
        return -1;
    }
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.eax = SYS_STATS;
    syscall_handler(&f);
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    if ((int32_t)f.eax != 0) {
        serial_puts("STATS-FAIL dispatch\n");
        return -1;
    }
    st = (struct nova_stats *)frame;
    /* Sane counters (threads: at least this boot thread's proc
     * graph; ticks advanced; frames free). */
    if (st->threads == 0 || st->pmm_free == 0) {
        serial_puts("STATS-FAIL values\n");
        return -1;
    }
    pmm_free_frame(frame);
    addrspace_destroy(as);
    return 0;
}

int stats_selftest(void) {
    if (stats_probe_user() != 0) {
        return -1;
    }
    serial_puts("STATS-OK\n");
    return NOVA_ESUCCESS;
}
