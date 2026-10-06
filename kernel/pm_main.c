/* NOVA OS 32-bit protected-mode entry (Phase 1 prototype).
 * Called once by stage2 trampoline as nova_pm_main(boot_info at 0x500).
 * Freestanding: no libc. Validates boot info, prints the boot marker and
 * the E820 memory map over COM1. Bad boot info is fatal (panic, not a
 * silent return). With NOVA_FAULT_TEST, takes the deliberate-panic path
 * used by `nova panic-test`. Otherwise halts via the trampoline.
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "mm/addrspace.h"
#include "irq/irq.h"
#include "time/time.h"
#include "sched/sched.h"
#include "syscall/syscall.h"
#include "fs/fs.h"
#include "fs/fat32.h"
#include "fs/ext4.h"
#include "fs/nova.h"
#include "net/net.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/fb.h"
#include "gui/comp.h"
#include "gui/font.h"
#include "gui/widget.h"
#include "input/kbd.h"
#include "input/mouse.h"
#include "net/sock.h"
#include "sync/sync.h"
#include "drivers/virtio_net.h"
#include "thread/thread.h"
#include "process/process.h"
#include "process/caps.h"
#include "process/rlimit.h"
#include "user.h"
#include "smp.h"
#include "../boot/boot_info.h"
#include "../boot/mem_layout.h"

extern uint8_t binary_init_bin_start[];
extern uint8_t binary_init_bin_end[];
extern uint8_t binary_hi_bin_start[];
extern uint8_t binary_hi_bin_end[];
extern uint8_t binary_uname_bin_start[];
extern uint8_t binary_uname_bin_end[];

/* Spawn a user image (Phase 7b: generalized from the Phase 6 init
 * spawn). Own process, PD, code and stack; entry is _ustart at
 * NOVA_USER_CODE (linker-asserted in every user binary). Returns the
 * pid, or 0 on any failure (OOM/bad size: caller decides panic vs
 * errno). The image bytes live in kernel memory (blob or FS node). */
static uint32_t spawn_image(const uint8_t *img, uint32_t size) {
    struct process *p;
    struct addrspace *as;
    uint32_t frames;
    uint32_t code;
    uint32_t stack;
    struct thread *t;
    p = process_create();
    as = addrspace_create();
    if (p == 0 || as == 0) {
        return 0;
    }
    /* Inherit credentials from the spawner (Phase 10a): the child
     * starts with no more rights than its parent. Kernel-context
     * spawns (no current thread) grant root/all, matching init. */
    {
        struct thread *cur = sched_current_thread();
        if (cur != 0 && cur->proc != 0) {
            p->caps = cur->proc->caps;
            p->uid = cur->proc->uid;
            p->gid = cur->proc->gid;
            p->rlimit_nofd = cur->proc->rlimit_nofd;
            p->rlimit_fsize = cur->proc->rlimit_fsize;
            p->rlimit_frames = cur->proc->rlimit_frames;
            p->rlimit_ticks = cur->proc->rlimit_ticks;
            p->jail_root = cur->proc->jail_root;
            /* The child holds its jail root too (else a parent's
             * chroot-away could drop the count to zero under it). */
            fs_node_ref(p->jail_root);
            p->caps = CAPS_ALL;
            p->uid = UID_ROOT;
        }
    }
    as->max_mapped = p->rlimit_frames;
    p->cr3 = as->pd;
    frames = (size + 4095u) / 4096u;
    if (size == 0 || frames > 64u) {
        return 0;
    }
    code = pmm_alloc_contig(frames);
    stack = pmm_alloc_contig(2);
    if (code == 0 || stack == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < size; i++) {
        ((uint8_t *)code)[i] = img[i];
    }
    for (uint32_t f = 0; f < frames; f++) {
        if (addrspace_map(as, NOVA_USER_CODE + f * 4096u,
                          code + f * 4096u, 1) != 0) {
            return 0; /* frame ceiling (Phase 10b): no partial child */
        }
    }
    if (addrspace_map(as, NOVA_USER_STACK_TOP - 8192u, stack, 1) !=
            0 ||
        addrspace_map(as, NOVA_USER_STACK_TOP - 4096u,
                      stack + 4096u, 1) != 0) {
        return 0;
    }
    t = thread_create_user(p, NOVA_USER_CODE, NOVA_USER_STACK_TOP,
                           stack, 0, 16u);
    if (t == 0) {
        return 0;
    }
    return p->pid;
}

/* Spawn the builtin init program (Phase 6: linked into the kernel;
 * exec-by-name arrives with the filesystem). */
static void spawn_init(void) {
    uint32_t size =
        (uint32_t)(binary_init_bin_end - binary_init_bin_start);
    if (spawn_image(binary_init_bin_start, size) == 0) {
        nova_panic("init-spawn-fail");
    }
    serial_puts("INIT-OK\n");
}

/* Seed builtin user binaries into ramfs (Phase 7b): the hi blob
 * becomes /bin/hi (exec proof + shell `run` target), the uname
 * blob /bin/uname (Phase 11a SDK sample). /disk is a
 * placeholder dir: paths under it route to the FAT driver (7c).
 * Runs after fs_init, before fs_selftest (which asserts the seed). */
static void seed_binaries(void) {
    struct fs_node *n = 0;
    uint32_t size = (uint32_t)(binary_hi_bin_end - binary_hi_bin_start);
    uint32_t usize =
        (uint32_t)(binary_uname_bin_end - binary_uname_bin_start);
    uint32_t got = 0;
    if (fs_mkdir("/bin") != 0) {
        nova_panic("bin-mkdir-fail");
    }
    if (fs_mkdir("/disk") != 0) {
        nova_panic("disk-mkdir-fail");
    }
    if (fs_mkdir("/ext") != 0) {
        nova_panic("ext-mkdir-fail");
    }
    if (fs_mkdir("/nova") != 0) {
        nova_panic("nova-mkdir-fail");
    }
    if (fs_create("/bin/hi", &n) != 0 || n == 0) {
        nova_panic("bin-create-fail");
    }
    if (fs_write_node(n, 0, binary_hi_bin_start, size, &got) != 0 ||
        got != size) {
        nova_panic("bin-seed-fail");
    }
    n = 0;
    if (fs_create("/bin/uname", &n) != 0 || n == 0) {
        nova_panic("uname-create-fail");
    }
    if (fs_write_node(n, 0, binary_uname_bin_start, usize, &got) !=
            0 ||
        got != usize) {
        nova_panic("uname-seed-fail");
    }
}

/* FAT sector source (Phase 7c): the data disk is the ATA primary
 * slave (P0D1). Read-only; errors surface as -EIO in fat_* calls. */
static int ata_slave_read(uint32_t lba, uint8_t *dst) {
    return ata_read_sector(ATA_DRIVE_SLAVE, lba, dst);
}

/* FAT sector sink (Phase 7d, same disk). Write-through; the FAT
 * layer invalidates its sector cache on every write. */
static int ata_slave_write(uint32_t lba, const uint8_t *src) {
    return ata_write_sector(ATA_DRIVE_SLAVE, lba, src);
}

/* EXT4 sector source (Phase 7e): secondary master (P1D0).
 * Read-only; no sink (writes are EROFS at the syscall layer). */
static int ata_ext_read(uint32_t lba, uint8_t *dst) {
    return ata_read_sector(ATA_DRIVE_SEC_MASTER, lba, dst);
}

/* NOVA-FS sector source (Phase 7f-1): secondary slave (P1D1). */
static int ata_nova_read(uint32_t lba, uint8_t *dst) {
    return ata_read_sector(ATA_DRIVE_SEC_SLAVE, lba, dst);
}

/* NOVA-FS sector sink (Phase 7f-2, same disk). Write-through; the
 * driver invalidates nothing cached (no read cache in nova.c). */
static int ata_nova_write(uint32_t lba, const uint8_t *src) {
    return ata_write_sector(ATA_DRIVE_SEC_SLAVE, lba, src);
}

/* Spawn a program stored in the filesystem (Phase 7b exec). Path
 * must name a regular file; its bytes become the new image. Returns
 * the child pid, or a negative -errno (never panics: user input). */
int32_t spawn_file(const char *path) {
    struct fs_node *n = 0;
    uint32_t pid;
    int rc = fs_lookup(path, &n);
    if (rc != 0) {
        return rc;
    }
    if (n->is_dir) {
        return -NOVA_EISDIR;
    }
    pid = spawn_image(n->data, n->size);
    if (pid == 0) {
        return -NOVA_ENOSPC;
    }
    return (int32_t)pid;
}

void nova_pm_main(const nova_boot_info_t *info) {
    serial_puts("NOVA-OS-BOOT-MARKER-v0\n");
#ifdef NOVA_VERSION
    serial_puts("NOVA-OS " NOVA_VERSION "\n");
#endif
    if (info->magic != NOVA_BOOT_MAGIC ||
        info->version != NOVA_BOOT_INFO_VERSION) {
        nova_panic("bad-boot-info");
    }
    serial_puts("boot-info ok regions=");
    serial_putdec32(info->region_count);
    serial_putc('\n');
    for (uint32_t i = 0; i < info->region_count; i++) {
        const nova_mem_region_t *r = &info->regions[i];
        serial_puts("mem base=0x");
        serial_puthex64(r->base);
        serial_puts(" len=0x");
        serial_puthex64(r->length);
        serial_puts(" type=");
        serial_putdec32(r->type);
        serial_putc('\n');
    }
    if (pmm_init(info, 0, 0) != 0) {
        nova_panic("pmm-no-arena");
    }
    serial_puts("PMM-");
    serial_puts(be_name());
    serial_puts(" total=");
    serial_putdec32(pmm_total_frames());
    serial_puts(" free=");
    serial_putdec32(pmm_free_count());
    serial_puts(" largest=");
    serial_putdec32(pmm_largest_free());
    serial_putc('\n');
    if (pmm_selftest() != 0) {
        nova_panic("pmm-selftest-fail");
    }
    serial_puts("PMM-OK\n");
    if (vmm_init() != 0) {
        nova_panic("vmm-init-fail");
    }
    if (vmm_selftest() != 0) {
        nova_panic("vmm-selftest-fail");
    }
    serial_puts("VMM-OK\n");
    kheap_init();
    {
        uint32_t slabs, live, large;
        kheap_stats(&slabs, &live, &large);
        serial_puts("HEAP slabs=");
        serial_putdec32(slabs);
        serial_puts(" live=");
        serial_putdec32(live);
        serial_puts(" large=");
        serial_putdec32(large);
        serial_putc('\n');
    }
    int heap_rc = kheap_selftest();
    if (heap_rc != 0) {
        serial_puts("HEAP-SELFTEST-FAIL code=");
        serial_putdec32((uint32_t)heap_rc);
        serial_putc('\n');
        nova_panic("heap-selftest-fail");
    }
    serial_puts("HEAP-OK\n");
    /* IDT first (Phase 7c lesson): slow polling (ATA PIO) crosses the
     * first BIOS timer tick (~55ms); without a valid IDT that IRQ
     * vectors through the IVT-as-IDT garbage and triple-faults. With
     * irq_init loaded, ticks vector to real handlers (sched gated). */
    irq_init();
    timer_init();
    syscall_init();
    ata_init();
    if (ata_selftest() != 0) {
        nova_panic("ata-selftest-fail");
    }
    if (pci_selftest() != 0) {
        nova_panic("pci-selftest-fail");
    }
    /* Framebuffer (Phase 12a): mode 13h is already live (stage1);
     * font first (Phase 12c: text tests need glyphs), then prove
     * pixels + palette + text, then leave the splash up. The VGA
     * text driver is retired from the boot path (its aperture is
     * planar in graphics mode, so cell readback cannot pass);
     * serial stays the console, its text mirror inert. */
    font_init();
    if (fb_selftest() != 0) {
        nova_panic("fb-selftest-fail");
    }
    fb_splash();
    /* Compositor (Phase 12b): prove layers + focus (the demo
     * scene paints later, after every selftest, so the pool is
     * deterministically empty here and for win_selftest). */
    if (comp_selftest() != 0) {
        nova_panic("comp-selftest-fail");
    }
    if (wid_selftest() != 0) {
        nova_panic("wid-selftest-fail");
    }
    /* Mouse (Phase 12e): 8042 + reset proof, then parser/click
     * proof on crafted bytes (real motion needs a hand on the
     * host; the IRQ path shares the tested feed function). */
    if (mouse_init() != 0) {
        nova_panic("mouse-init-fail");
    }
    if (mouse_selftest() != 0) {
        nova_panic("mouse-selftest-fail");
    }
    if (virtio_net_init() != 0) {
        nova_panic("virtio-init-fail");
    }
    if (virtio_net_selftest() != 0) {
        nova_panic("virtio-selftest-fail");
    }
    if (fs_init() != 0) {
        nova_panic("fs-init-fail");
    }
    seed_binaries();
    if (fs_selftest() != 0) {
        nova_panic("fs-selftest-fail");
    }
    if (fat_init(ata_slave_read, ata_slave_write) != 0) {
        nova_panic("fat-init-fail");
    }
    if (fat_selftest() != 0) {
        nova_panic("fat-selftest-fail");
    }
    if (ext_init(ata_ext_read) != 0) {
        nova_panic("ext-init-fail");
    }
    if (ext_selftest() != 0) {
        nova_panic("ext-selftest-fail");
    }
    if (nova_init(ata_nova_read, ata_nova_write) != 0) {
        nova_panic("nova-init-fail");
    }
    if (nova_selftest() != 0) {
        nova_panic("nova-selftest-fail");
    }
    if (net_init() != 0) {
        nova_panic("net-init-fail");
    }
    if (net_selftest() != 0) {
        nova_panic("net-selftest-fail");
    }
    if (caps_selftest() != 0) {
        nova_panic("caps-selftest-fail");
    }
    if (rlim_selftest() != 0) {
        nova_panic("rlim-selftest-fail");
    }
    if (perm_selftest() != 0) {
        nova_panic("perm-selftest-fail");
    }
    if (jail_selftest() != 0) {
        nova_panic("jail-selftest-fail");
    }
    if (win_selftest() != 0) {
        nova_panic("win-selftest-fail");
    }
    if (sock_selftest() != 0) {
        nova_panic("sock-selftest-fail");
    }
    {
        int sched_rc = sched_selftest();
        if (sched_rc != 0) {
            serial_puts("SCHED-SELFTEST-FAIL code=");
            serial_putdec32((uint32_t)sched_rc);
            serial_putc('\n');
            nova_panic("sched-selftest-fail");
        }
    }
    /* Sleep/wakeup primitives (scheduler + timer live now). */
    if (sched_sleep_selftest() != 0) {
        nova_panic("sleep-selftest-fail");
    }
    /* Starvation measurement (strict-prio semantics + numbers). */
    if (sched_starve_selftest() != 0) {
        nova_panic("starve-selftest-fail");
    }
    /* Blocking sockets (threads exist now; proves park/wake). */
    if (sock_block_selftest() != 0) {
        nova_panic("sockblock-selftest-fail");
    }
    /* IRQ-path pump drill (quiet ring, after all wire flows). */
    if (sock_drill_selftest() != 0) {
        nova_panic("sockdrill-selftest-fail");
    }
    /* Blocking keyboard (reader threads exist now). */
    if (kbd_block_selftest() != 0) {
        nova_panic("kbdblock-selftest-fail");
    }
    /* SMP foundations: lock contention under preemption. */
    if (sync_contention_selftest() != 0) {
        nova_panic("sync-selftest-fail");
    }
    /* Stats dispatch (real user page). */
    if (stats_selftest() != 0) {
        nova_panic("stats-selftest-fail");
    }
    {
        int user_rc = user_selftest();
        if (user_rc != 0) {
            serial_puts("USER-SELFTEST-FAIL code=");
            serial_putdec32((uint32_t)user_rc);
            serial_putc('\n');
            nova_panic("user-selftest-fail");
        }
    }
    {
        int usched_rc = user_sched_test();
        if (usched_rc != 0) {
            nova_panic("usched-selftest-fail");
        }
    }
    if (sched_ticklimit_selftest() != 0) {
        nova_panic("ticklimit-selftest-fail");
    }
    smp_boot();
    /* Demo scene (Phase 12b/12d): every selftest is done and the
     * pool is empty, so ids pack 0,1,2 deterministically. This
     * repaints the final screen (over the splash). */
    comp_demo();
    spawn_init();
    /* Boot-time exec proof (Phase 7b): run /bin/hi from the FS. Its
     * HI-OK line in the log proves spawn-by-name end to end. */
    if (spawn_file("/bin/hi") < 0) {
        nova_panic("exec-proof-fail");
    }
    /* SDK sample proof (Phase 11a): run /bin/uname from the FS. Its
     * SDK-OK line proves a third program built purely against
     * sdk/include links, seeds, spawns, and calls home. */
    if (spawn_file("/bin/uname") < 0) {
        nova_panic("sdk-proof-fail");
    }
    /* Formal gate summary (Phase 0.3-G): reaching here means every
     * selftest above passed (any failure panics first). Machine-
     * greppable SYSTEM READY line for harnesses and CI. */
    serial_puts("GATES-OK\n");
#ifdef NOVA_FAULT_TEST
    serial_puts("INJECT-FAULT\n");
    __asm__ volatile("ud2"); /* deliberate #UD: proves IDT->trap->panic */
    nova_panic("fault-test-unreached");
#else
    /* Idle forever (timer keeps ticking for init/shell); the old
     * halt-on-return path would kill preemption, so never return.
     * Strict priority (no aging yet) means the prio-16 shell starves
     * this thread while it spins: expected, see Phase 4d. */
    __asm__ volatile("sti");
    for (;;) {
        __asm__ volatile("hlt");
    }
#endif
}
