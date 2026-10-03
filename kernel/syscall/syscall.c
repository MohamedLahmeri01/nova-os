/* NOVA OS syscall dispatch + handlers (Phase 5 ABI v1, Phase 7 files).
 * Table-driven (new versions add rows). Every pointer/length goes
 * through validate_usermem; failures return -errno, never panic.
 * Handler costs are bounded (print is cli-guarded, no shared buffer;
 * file I/O chunks through a 512B bounce buffer).
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "syscall/syscall.h"
#include "syscall/validate.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "fs/fs.h"
#include "fs/fat32.h"
#include "irq/irq.h"
#include "input/kbd.h"
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
    int rc;
    (void)c;
    g_sysc_print++;
    if (len > SYSCALL_MAX_PRINT) {
        return -NOVA_EINVAL;
    }
    rc = validate_usermem(ptr, len, 0);
    if (rc != 0) {
        return rc;
    }
    __asm__ volatile("cli" ::: "memory");
    for (uint32_t i = 0; i < len; i++) {
        serial_putc((char)*(volatile uint8_t *)(ptr + i));
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
    /* /disk graft (Phase 7c): FAT is read-only; materialize the whole
     * file into a detached node owned by the description. */
    if (fs_is_disk_path(kpath)) {
        uint8_t *data;
        uint32_t size = 0;
        uint32_t got = 0;
        int is_dir = 0;
        struct fs_node *n;
        if ((flags & FS_O_ACCMODE) != FS_O_RDONLY ||
            (flags & FS_O_CREAT) != 0) {
            return -NOVA_EROFS;
        }
        rc = fat_stat(fs_disk_rel(kpath), &size, &is_dir);
        if (rc != 0) {
            return rc;
        }
        if (is_dir) {
            return -NOVA_EISDIR;
        }
        if (size > FS_MAX_FILE) {
            return -NOVA_ENOSPC;
        }
        n = (struct fs_node *)kmalloc(sizeof(*n));
        data = (size != 0) ? (uint8_t *)kmalloc(size) : 0;
        if (n == 0 || (size != 0 && data == 0)) {
            if (n != 0) {
                kfree(n);
            }
            if (data != 0) {
                kfree(data);
            }
            return -NOVA_ENOSPC;
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
        n->cap = size;
        rc = fat_read_file(fs_disk_rel(kpath), data, size, &got);
        if (rc != 0 || got != size) {
            kfree(n);
            if (data != 0) {
                kfree(data);
            }
            return (rc != 0) ? rc : -NOVA_EIO;
        }
        n->size = size;
        f = fs_file_alloc(n, flags);
        if (f == 0) {
            kfree(n);
            if (data != 0) {
                kfree(data);
            }
            return -NOVA_ENOSPC;
        }
        f->owns_node = 1;
        rc = fs_fd_alloc(p, f);
        if (rc < 0) {
            fs_file_free(f);
            return rc;
        }
        return rc;
    }
    rc = fs_lookup(kpath, &n);
    if (rc == -NOVA_ENOENT && (flags & FS_O_CREAT) != 0) {
        rc = fs_create(kpath, &n);
    }
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
        uint32_t i;
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
        for (i = 0; i < got; i++) {
            *(volatile uint8_t *)(buf + total + i) = kbuf[i];
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
    while (total < len) {
        uint32_t want = len - total;
        uint32_t got = 0;
        uint32_t i;
        int rc;
        if (want > sizeof(kbuf)) {
            want = sizeof(kbuf);
        }
        for (i = 0; i < want; i++) {
            kbuf[i] = *(volatile uint8_t *)(buf + total + i);
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

static int32_t do_readdir(uint32_t path, uint32_t index, uint32_t namebuf) {
    char kpath[FS_MAX_PATH + 1u];
    char name[FS_MAX_NAME];
    struct fs_node *dir = 0;
    uint32_t i;
    int32_t plen;
    int rc;
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    if (validate_usermem(namebuf, FS_MAX_NAME, 1) != 0) {
        return -NOVA_EFAULT;
    }
    /* /disk graft: FAT listing (rel "" = FAT root). */
    if (fs_is_disk_path(kpath)) {
        char kname[FS_MAX_NAME];
        int r = fat_list_dir(fs_disk_rel(kpath), index, kname);
        if (r < 0) {
            return r;
        }
        for (int32_t i = 0; i <= r; i++) {
            *(volatile char *)(namebuf + (uint32_t)i) = kname[i];
        }
        return r;
    }
    rc = fs_lookup(kpath, &dir);
    if (rc != 0) {
        return rc;
    }
    rc = fs_readdir(dir, index, name);
    if (rc < 0) {
        return rc;
    }
    for (i = 0; i <= (uint32_t)rc; i++) {
        *(volatile char *)(namebuf + i) = name[i];
    }
    return rc;
}

static int32_t do_mkdir(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)b;
    (void)c;
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    if (fs_is_disk_path(kpath)) {
        return -NOVA_EROFS;
    }
    return fs_mkdir(kpath);
}

static int32_t do_exec(uint32_t path, uint32_t b, uint32_t c) {
    char kpath[FS_MAX_PATH + 1u];
    int32_t plen;
    (void)b;
    (void)c;
    plen = copy_user_path(path, kpath);
    if (plen < 0) {
        return plen;
    }
    /* Spawn semantics (no image replacement yet): a new process runs
     * the file's bytes; the caller keeps running. Returns child pid. */
    return spawn_file(kpath);
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
