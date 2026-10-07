/* NOVA OS microbenchmark implementation (Phase 0.4-P).
 * Each bench warms nothing and measures ticks around a fixed
 * workload; all inputs fit statics/stack (no alloc failure
 * modes inside the timed region except the heap bench itself).
 */
#include <stdint.h>

#include "bench/bench.h"
#include "serial.h"
#include "syscall/syscall.h"
#include "syscall/validate.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "mm/addrspace.h"
#include "thread/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "sync/sync.h"
#include "net/sock.h"
#include "net/net.h"
#include "fs/fs.h"
#include "trace/trace.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "bench: no arch backend for this architecture"
#endif

static void perf_line(const char *name, uint32_t ops,
                      uint32_t ticks) {
    serial_puts("PERF ");
    serial_puts(name);
    serial_puts(" ops=");
    serial_putdec32(ops);
    serial_puts(" ticks=");
    serial_putdec32(ticks);
    serial_putc('\n');
}

/* --- ctx switches: two threads ping-pong yields --- */
static volatile uint32_t bx_done;
static volatile uint32_t bx_turn;

static void bench_ping(void *arg) {
    uint32_t me = (uint32_t)(uintptr_t)arg;
    for (uint32_t i = 0; i < 1000u; i++) {
        while (bx_turn != me) {
            sched_yield();
        }
        bx_turn = 3u - me;
    }
    arch_atomic_add_u32(&bx_done, 1u);
    thread_exit(0);
}

/* --- thread create/exit churn --- */
static volatile uint32_t bn_done;

static void bench_nop(void *arg) {
    (void)arg;
    arch_atomic_add_u32(&bn_done, 1u);
    thread_exit(0);
}

int bench_selftest(void) {
    extern volatile uint32_t g_ticks;
    uint32_t t0, t1;
    /* 1. ctx switches: ping-pong pair, 1000 volleys each
     * (every volley is 2 switches + 2 yields). */
    {
        struct thread *a;
        struct thread *b;
        bx_done = 0;
        bx_turn = 1u;
        a = thread_create(g_init_process, bench_ping, (void *)1,
                          16u);
        b = thread_create(g_init_process, bench_ping, (void *)2,
                          16u);
        if (a == 0 || b == 0) {
            serial_puts("PERF-FAIL spawn-ctx\n");
            return -1;
        }
        t0 = g_ticks;
        while (bx_done != 2u) {
            if (g_ticks - t0 > 8000u) {
                serial_puts("PERF-FAIL ctx\n");
                return -1;
            }
            __asm__ volatile("hlt");
        }
        t1 = g_ticks;
        perf_line("ctx", 4000u, t1 - t0);
        if (t1 == t0) {
            serial_puts("PERF-FAIL ctx-zero\n");
            return -1;
        }
    }
    /* 2. heap churn: 500 alloc/free pairs, 64B class. */
    {
        t0 = g_ticks;
        for (uint32_t i = 0; i < 100000u; i++) {
            void *p = kmalloc(64);
            if (p == 0) {
                serial_puts("PERF-FAIL heap-oom\n");
                return -1;
            }
            kfree(p);
        }
        t1 = g_ticks;
        perf_line("heap", 200000u, t1 - t0);
    }
    /* 3. uncontended lock: 200k acquire/release pairs. */
    {
        static struct spinlock lk;
        uint32_t was = 0;
        spin_init(&lk, "bench");
        t0 = g_ticks;
        for (uint32_t i = 0; i < 2000000u; i++) {
            spin_lock_irqsave(&lk, 0xBEu, &was);
            spin_unlock_irqrestore(&lk, was);
        }
        t1 = g_ticks;
        perf_line("lock", 4000000u, t1 - t0);
    }
    /* 4. loopback sockets: 200 send/recv round-trips. */
    {
        static const uint8_t pl[8] = { 'p', 'e', 'r', 'f',
                                       '0', '1', '2', '3' };
        static uint8_t rx[64];
        int a = sock_open(5200);
        int b = sock_open(5201);
        if (a < 0 || b < 0) {
            serial_puts("PERF-FAIL sock-open\n");
            return -1;
        }
        t0 = g_ticks;
        for (uint32_t i = 0; i < 20000u; i++) {
            uint32_t g = 0;
            if (sock_send((uint32_t)a, 0x7F000001u, 5201, pl,
                         sizeof(pl)) != 0 ||
                sock_recv((uint32_t)b, rx, sizeof(rx), 100,
                          &g) != 8) {
                serial_puts("PERF-FAIL sock-rt\n");
                return -1;
            }
        }
        t1 = g_ticks;
        perf_line("sock", 40000u, t1 - t0);
        if (sock_close((uint32_t)a) != 0 ||
            sock_close((uint32_t)b) != 0) {
            serial_puts("PERF-FAIL sock-close\n");
            return -1;
        }
    }
    /* 5. ramfs files: 20 × (create + 4KB write + readback +
     * delete). Exercises create/write/read/unlink end to end. */
    {
        static uint8_t wbuf[4096];
        static uint8_t rbuf[4096];
        for (uint32_t i = 0; i < sizeof(wbuf); i++) {
            wbuf[i] = (uint8_t)(i & 0xFFu);
        }
        t0 = g_ticks;
        for (uint32_t f = 0; f < 20u; f++) {
            char name[16];
            struct fs_node *n = 0;
            uint32_t off = 0;
            name[0] = '/';
            name[1] = 'p';
            name[2] = (char)('0' + (f / 10u));
            name[3] = (char)('0' + (f % 10u));
            name[4] = 0;
            if (fs_create(name, &n) != 0) {
                serial_puts("PERF-FAIL fs-create\n");
                return -1;
            }
            while (off < sizeof(wbuf)) {
                uint32_t g = 0;
                if (fs_write_node(n, off, wbuf + off,
                                  sizeof(wbuf) - off, &g) != 0 ||
                    g == 0) {
                    serial_puts("PERF-FAIL fs-write\n");
                    return -1;
                }
                off += g;
            }
            off = 0;
            while (off < sizeof(rbuf)) {
                uint32_t g = 0;
                if (fs_read_node(n, off, rbuf + off,
                                 sizeof(rbuf) - off, &g) != 0 ||
                    g == 0) {
                    serial_puts("PERF-FAIL fs-read\n");
                    return -1;
                }
                off += g;
            }
            for (uint32_t i = 0; i < sizeof(wbuf); i++) {
                if (rbuf[i] != wbuf[i]) {
                    serial_puts("PERF-FAIL fs-bytes\n");
                    return -1;
                }
            }
            if (fs_unlink(name) != 0) {
                serial_puts("PERF-FAIL fs-unlink\n");
                return -1;
            }
        }
        t1 = g_ticks;
        perf_line("fs", 80u, t1 - t0);
    }
    /* 6. user copy: 100 x 4KB through a scratch mapping
     * (stats-test pattern: clone-kernel PD, US/RW page). */
    {
        struct addrspace *as;
        uint32_t f;
        static uint8_t kbuf[4096];
        uint32_t saved;
        as = addrspace_create();
        f = pmm_alloc_frame();
        if (as == 0 || f == 0) {
            serial_puts("PERF-FAIL copy-alloc\n");
            return -1;
        }
        if (addrspace_map(as, USER_BASE, f, 1) != 0) {
            serial_puts("PERF-FAIL copy-map\n");
            return -1;
        }
        for (uint32_t i = 0; i < sizeof(kbuf); i++) {
            kbuf[i] = (uint8_t)(i & 0x7Fu);
        }
        __asm__ volatile("cli" ::: "memory");
        saved = arch_read_cr3();
        arch_write_cr3(as->pd);
        t0 = g_ticks;
        for (uint32_t i = 0; i < 1000u; i++) {
            if (copy_to_user(USER_BASE, kbuf, sizeof(kbuf)) != 0) {
                arch_write_cr3(saved);
                __asm__ volatile("sti" ::: "memory");
                serial_puts("PERF-FAIL copy-out\n");
                return -1;
            }
            if (copy_from_user(kbuf, USER_BASE, sizeof(kbuf)) !=
                0) {
                arch_write_cr3(saved);
                __asm__ volatile("sti" ::: "memory");
                serial_puts("PERF-FAIL copy-in\n");
                return -1;
            }
        }
        t1 = g_ticks;
        arch_write_cr3(saved);
        __asm__ volatile("sti" ::: "memory");
        pmm_free_frame(f);
        addrspace_destroy(as);
        perf_line("copy", 2000u, t1 - t0);
    }
    /* 7. trace fetch: 200 x full-ring (256) fetch through
     * real dispatch on a scratch mapping (ring pre-filled;
     * measures snapshot + project + copy, the debuggability
     * price per full read). */
    {
        struct addrspace *as;
        uint32_t f1, f2, saved;
        struct syscall_frame fr;
        as = addrspace_create();
        f1 = pmm_alloc_frame();
        f2 = pmm_alloc_frame();
        if (as == 0 || f1 == 0 || f2 == 0) {
            serial_puts("PERF-FAIL trace-alloc\n");
            return -1;
        }
        if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
            addrspace_map(as, USER_BASE + 0x1000u, f2, 1) != 0) {
            serial_puts("PERF-FAIL trace-map\n");
            return -1;
        }
        for (uint32_t i = 0; i < TRACEFETCH_MAX; i++) {
            trace_emit(TRACE_MARK, (uint16_t)i, i);
        }
        fr.ebx = USER_BASE;
        fr.ecx = TRACEFETCH_MAX;
        fr.edx = 0;
        __asm__ volatile("cli" ::: "memory");
        saved = arch_read_cr3();
        arch_write_cr3(as->pd);
        t0 = g_ticks;
        for (uint32_t i = 0; i < 200u; i++) {
            /* eax is IN+OUT (number in, result out): restore the
             * number every iteration or the second fetch dispatches
             * syscall#256 (ENOSYS). */
            fr.eax = SYS_TRACEFETCH;
            syscall_handler(&fr);
            if ((int32_t)fr.eax != (int32_t)TRACEFETCH_MAX) {
                arch_write_cr3(saved);
                __asm__ volatile("sti" ::: "memory");
                serial_puts("PERF-FAIL trace-fetch rc=0x");
                serial_puthex32(fr.eax);
                serial_puts(" iter=");
                serial_putdec32(i);
                serial_putc('\n');
                return -1;
            }
        }
        t1 = g_ticks;
        arch_write_cr3(saved);
        __asm__ volatile("sti" ::: "memory");
        pmm_free_frame(f1);
        pmm_free_frame(f2);
        addrspace_destroy(as);
        perf_line("trace", 200u, t1 - t0);
    }
    /* 8. thread create/exit churn: 20 spawns, joined. */
    {
        t0 = g_ticks;
        for (uint32_t i = 0; i < 100u; i++) {
            uint32_t t9 = g_ticks;
            bn_done = 0;
            if (thread_create(g_init_process, bench_nop, 0, 16u) ==
                0) {
                serial_puts("PERF-FAIL spawn\n");
                return -1;
            }
            sched_yield();
            while (bn_done != 1u) {
                if (g_ticks - t9 > 8000u) {
                    serial_puts("PERF-FAIL join\n");
                    return -1;
                }
                __asm__ volatile("hlt");
            }
            thread_reap_graveyard();
        }
        t1 = g_ticks;
        perf_line("thread", 100u, t1 - t0);
    }
    /* 9. syscall dispatch floor: 100k TICKS through the real
     * handler (table walk included; boot context suffices —
     * do_ticks touches no process state). */
    {
        struct syscall_frame f;
        volatile uint32_t sink = 0;
        f.kflags = 0;
        f.edi = 0;
        f.esi = 0;
        f.ebp = 0;
        f.esp_ignored = 0;
        f.ebx = 0;
        f.edx = 0;
        f.ecx = 0;
        f.eax = SYS_TICKS;
        f.eip = 0;
        f.cs = 0;
        f.eflags = 0;
        f.esp = 0;
        f.ss = 0;
        t0 = g_ticks;
        for (uint32_t i = 0; i < 1000000u; i++) {
            /* Same eax IN+OUT discipline as the trace bench
             * above: without this the loop times the ENOSYS
             * refusal path, not dispatch. */
            f.eax = SYS_TICKS;
            syscall_handler(&f);
            sink = f.eax;
        }
        t1 = g_ticks;
        (void)sink;
        perf_line("call", 1000000u, t1 - t0);
    }
    serial_puts("PERF-OK\n");
    return NOVA_ESUCCESS;
}

/* --- thread-pool exhaustion (Phase L): fill to THREAD_MAX,
 * prove the cap refuses, drain, prove recovery. The fill runs
 * cli'd: ticks would otherwise preempt the creator and recycle
 * the pool through exits (no sleep/hlt/switch inside the fill,
 * so cli is safe here). */
static volatile uint32_t tx_done;

static void threx_worker(void *arg) {
    (void)arg;
    arch_atomic_add_u32(&tx_done, 1u);
    thread_exit(0);
}

int threx_selftest(void) {
    extern volatile uint32_t g_ticks;
    uint32_t before = thread_count();
    uint32_t created = 0;
    uint32_t t0;
    tx_done = 0;
    __asm__ volatile("cli" ::: "memory");
    for (;;) {
        struct thread *t = thread_create(g_init_process, threx_worker,
                                         0, 16u);
        if (t == 0) {
            break;
        }
        created++;
        if (created > THREAD_MAX) {
            __asm__ volatile("sti" ::: "memory");
            serial_puts("THREX-FAIL overfill\n");
            return -1;
        }
    }
    __asm__ volatile("sti" ::: "memory");
    if (created == 0) {
        serial_puts("THREX-FAIL no-slot\n");
        return -1;
    }
    if (before + created != THREAD_MAX) {
        serial_puts("THREX-FAIL count\n");
        return -1;
    }
    /* Drain: every worker runs once and exits. */
    t0 = g_ticks;
    while (tx_done != created) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("THREX-FAIL drain\n");
            return -1;
        }
        sched_yield();
    }
    thread_reap_graveyard();
    if (thread_count() != before) {
        serial_puts("THREX-FAIL leak\n");
        return -1;
    }
    /* Recovery: the freed pool takes a thread again. */
    tx_done = 0;
    if (thread_create(g_init_process, threx_worker, 0, 16u) == 0) {
        serial_puts("THREX-FAIL recovery\n");
        return -1;
    }
    t0 = g_ticks;
    while (tx_done != 1u) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("THREX-FAIL rejoin\n");
            return -1;
        }
        sched_yield();
    }
    thread_reap_graveyard();
    if (thread_count() != before) {
        serial_puts("THREX-FAIL releak\n");
        return -1;
    }
    serial_puts("THREX-OK burst=");
    serial_putdec32(created);
    serial_putc('\n');
    return NOVA_ESUCCESS;
}

/* --- cross-subsystem integration (Phase L, Scenarios A–F core):
 * one RR pair interleaving fs content + fd lifecycle + sock
 * churn + RX pump + trace + runtime queries for 40 rounds,
 * byte-exact throughout. Any interleave must preserve every
 * assertion: that is the whole-system claim. */
#define XSUB_ROUNDS 40u

static volatile uint32_t xs_done;
static volatile uint32_t xs_fail;

static void xsub_worker(void *arg) {
    uint32_t i;
    (void)arg;
    for (i = 0; i < XSUB_ROUNDS; i++) {
        int id = sock_open(0);
        if (id < 0) {
            /* Pool is 8, both threads hold <= 1: EMFILE here
             * is a real defect, not pressure. */
            xs_fail = 1;
            thread_exit(1);
        }
        if (sock_close((uint32_t)id) != 0) {
            xs_fail = 1;
            thread_exit(1);
        }
        sched_yield();
    }
    xs_done = 1;
    thread_exit(0);
}

int xsub_selftest(void) {
    extern volatile uint32_t g_ticks;
    static uint8_t xw[64], xr[64];
    struct fs_node *n;
    struct thread *me = sched_current_thread();
    uint32_t rt0 = 0, rt1 = 0, pr = 0;
    int st = 0;
    uint32_t t0;
    uint32_t i, k;
    xs_done = 0;
    xs_fail = 0;
    if (me == 0) {
        serial_puts("XSUB-FAIL no-thread\n");
        return -1;
    }
    if (fs_create("/xsub", &n) != 0) {
        serial_puts("XSUB-FAIL create\n");
        return -1;
    }
    if (sched_thread_stat(me->id, &rt0, &st, &pr) != 1) {
        serial_puts("XSUB-FAIL stat0\n");
        return -1;
    }
    /* Same-prio companion: RR forces true interleaving. */
    if (thread_create(g_init_process, xsub_worker, 0, me->prio) ==
        0) {
        serial_puts("XSUB-FAIL spawn\n");
        return -1;
    }
    for (i = 0; i < XSUB_ROUNDS; i++) {
        uint32_t nw = 0, nr = 0;
        struct fs_file *f;
        int fd, id;
        for (k = 0; k < sizeof(xw); k++) {
            xw[k] = (uint8_t)(i + k);
        }
        if (fs_write_node(n, 0, xw, sizeof(xw), &nw) != 0 ||
            nw != sizeof(xw)) {
            serial_puts("XSUB-FAIL write\n");
            return -1;
        }
        if (fs_read_node(n, 0, xr, sizeof(xr), &nr) != 0 ||
            nr != sizeof(xr)) {
            serial_puts("XSUB-FAIL read\n");
            return -1;
        }
        for (k = 0; k < sizeof(xr); k++) {
            if (xr[k] != (uint8_t)(i + k)) {
                serial_puts("XSUB-FAIL content\n");
                return -1;
            }
        }
        f = fs_file_alloc(n, FS_O_RDWR);
        if (f == 0) {
            serial_puts("XSUB-FAIL alloc\n");
            return -1;
        }
        fd = fs_fd_alloc(g_init_process, f);
        if (fd < 0) {
            fs_file_free(f);
            serial_puts("XSUB-FAIL fd\n");
            return -1;
        }
        /* Drop frees the description (ref balanced). */
        if (fs_fd_drop(g_init_process, (uint32_t)fd) != 0) {
            serial_puts("XSUB-FAIL drop\n");
            return -1;
        }
        id = sock_open(0);
        if (id < 0) {
            serial_puts("XSUB-FAIL sock\n");
            return -1;
        }
        if (sock_close((uint32_t)id) != 0) {
            serial_puts("XSUB-FAIL close\n");
            return -1;
        }
        /* Live-device drain inside the mix (MMIO window +
         * rx_lock under scheduler preemption). */
        if (net_rx_pump() < 0) {
            serial_puts("XSUB-FAIL pump\n");
            return -1;
        }
        sched_yield();
    }
    t0 = g_ticks;
    while (xs_done == 0) {
        if (g_ticks - t0 > 8000u) {
            serial_puts("XSUB-FAIL join\n");
            return -1;
        }
        sched_yield();
    }
    if (xs_fail != 0) {
        serial_puts("XSUB-FAIL worker\n");
        return -1;
    }
    thread_reap_graveyard();
    if (sched_thread_stat(me->id, &rt1, &st, &pr) != 1 ||
        rt1 < rt0) {
        serial_puts("XSUB-FAIL stat1\n");
        return -1;
    }
    if (trace_count() > 256u) {
        serial_puts("XSUB-FAIL tracecap\n");
        return -1;
    }
    if (fs_unlink("/xsub") != 0) {
        serial_puts("XSUB-FAIL unlink\n");
        return -1;
    }
    serial_puts("XSUB-OK rounds=");
    serial_putdec32(XSUB_ROUNDS);
    serial_putc('\n');
    return NOVA_ESUCCESS;
}
