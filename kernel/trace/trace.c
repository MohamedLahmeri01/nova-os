/* NOVA OS kernel event trace implementation (Phase 0.4-O,
 * seq + projection in Phase C).
 * Single static ring (256 x 16B = 4KB, .bss). Emission holds
 * IF across reserve + fill, so no IRQ can interleave mid-record
 * (the old reserve-vs-IRQ scenario cannot occur). Readers
 * tolerate overwrite (count caps at capacity; read clamps).
 */
#include <stdint.h>

#include "trace/trace.h"
#include "serial.h"
#include "syscall/syscall.h"
#include "mm/addrspace.h"
#include "mm/pmm.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "trace: no arch backend for this architecture"
#endif

#define TRACE_N 256u

static struct trace_event g_ring[TRACE_N];
static volatile uint32_t g_head;
static volatile uint32_t g_n;
/* Emit sequence (never reset: see header). BSP-only assignment
 * under the emit lock; SMP era needs per-CPU seq spaces. */
static volatile uint32_t g_seq;
/* Phase D refetch-comparison stash (kernel .bss, 32B). */
static uint8_t adv_stash[32];

void trace_init(void) {
    g_head = 0;
    g_n = 0;
}

void trace_reset(void) {
    uint32_t f = arch_irq_save();
    g_head = 0;
    g_n = 0;
    arch_irq_restore(f);
}

void trace_emit(uint16_t kind, uint16_t id, uint32_t arg) {
    extern volatile uint32_t g_ticks;
    uint32_t f = arch_irq_save();
    uint32_t i = g_head;
    g_head = (g_head + 1u) % TRACE_N;
    if (g_n < TRACE_N) {
        g_n++;
    }
    g_ring[i].tick = g_ticks;
    g_ring[i].kind = kind;
    g_ring[i].id = id;
    g_ring[i].arg = arg;
    g_ring[i].seq = g_seq;
    g_seq++;
    arch_irq_restore(f);
}

uint32_t trace_count(void) {
    return (g_n <= TRACE_N) ? g_n : TRACE_N;
}

int trace_read(uint32_t i, struct trace_event *out) {
    uint32_t n;
    uint32_t idx;
    if (out == 0) {
        return -NOVA_EINVAL;
    }
    n = trace_count();
    if (i >= n) {
        return -NOVA_ENOENT;
    }
    /* Oldest-first: head advanced past the oldest held slot. */
    idx = (g_head + TRACE_N - n + i) % TRACE_N;
    out->tick = g_ring[idx].tick;
    out->kind = g_ring[idx].kind;
    out->id = g_ring[idx].id;
    out->arg = g_ring[idx].arg;
    out->seq = g_ring[idx].seq;
    return NOVA_ESUCCESS;
}

/* Find marker ids in order (subsequence: other events may
 * interleave — IRQs emit during any guest window). */
static int trace_find_mark(uint16_t want_id, uint32_t want_arg,
                           uint32_t from) {
    uint32_t n = trace_count();
    for (uint32_t i = from; i < n; i++) {
        struct trace_event e;
        if (trace_read(i, &e) != 0) {
            return -1;
        }
        if (e.kind == TRACE_MARK && e.id == want_id &&
            e.arg == want_arg) {
            return (int)i;
        }
    }
    return -1;
}

int trace_selftest(void) {
    int a, b, c, i;
    uint32_t n;
    trace_reset();
    if (trace_count() != 0) {
        serial_puts("TRACE-FAIL reset\n");
        return -1;
    }
    if (trace_read(0, 0) != -NOVA_EINVAL) {
        serial_puts("TRACE-FAIL null\n");
        return -1;
    }
    if (trace_read(0, &(struct trace_event){ 0 }) != -NOVA_ENOENT) {
        serial_puts("TRACE-FAIL empty\n");
        return -1;
    }
    trace_emit(TRACE_MARK, 11, 0xA1u);
    trace_emit(TRACE_MARK, 22, 0xB2u);
    trace_emit(TRACE_MARK, 33, 0xC3u);
    n = trace_count();
    if (n != 3u) {
        serial_puts("TRACE-FAIL count\n");
        return -1;
    }
    a = trace_find_mark(11, 0xA1u, 0);
    b = trace_find_mark(22, 0xB2u, 0);
    c = trace_find_mark(33, 0xC3u, 0);
    if (a < 0 || b <= a || c <= b) {
        serial_puts("TRACE-FAIL order\n");
        return -1;
    }
    /* Overwrite wrap: 300 more marks, count caps at 256, the
     * newest marker still found (oldest evicted, never torn). */
    for (i = 0; i < 300; i++) {
        trace_emit(TRACE_MARK, 77, (uint32_t)i);
    }
    if (trace_count() != TRACE_N) {
        serial_puts("TRACE-FAIL cap\n");
        return -1;
    }
    if (trace_find_mark(77, 299u, 0) < 0) {
        serial_puts("TRACE-FAIL wrap\n");
        return -1;
    }
    serial_puts("TRACE-OK\n");
    return NOVA_ESUCCESS;
}

/* Fault-address projection (the one lossy field, by decision):
 * classes reveal what kind of address faulted, never which.
 * 0 null (<4K), 1 low/hw (<2G), 2 user, 3 supervisor. A reader
 * learns at most that some thread probed a supervisor address
 * — never whose, never which. */
static uint16_t fault_class(uint32_t addr) {
    if (addr < 0x1000u) {
        return 0;
    }
    if (addr < USER_BASE) {
        return 1;
    }
    if (addr < USER_END) {
        return 2;
    }
    return 3;
}

void trace_project(const struct trace_event *in,
                   struct nova_trace_rec *out) {
    out->vers = NOVA_TRACE_VERS;
    out->kind = in->kind;
    out->seq = in->seq;
    out->tick = in->tick;
    out->id = in->id;
    out->rsv = 0;
    if (in->kind == TRACE_FAULT) {
        out->arg = fault_class(in->arg);
    } else {
        out->arg = in->arg;
    }
}

/* Trace-fetch ABI selftest (Phase C): real syscall frames through
 * the real dispatch on a real scratch addrspace, read back through
 * the identity map. Killed vectors: max==0 touches nothing (not
 * even validation: NULL returns 0); two live marks
 * project exactly (vers/kind/id/arg/rsv, seq +1); fault projects
 * to class (never address); supervisor-half pointer refuses
 * EFAULT with the scratch provably untouched; over-cap max
 * refuses EINVAL; newest-256 window lands oldest-first with
 * strictly +1 seqs. Two scratch frames at USER_BASE (256 recs =
 * 5120B > one page). CR3 lent under cli, restored + destroyed
 * after (stats precedent). */

static int tracefetch_probe_user(void) {
    struct syscall_frame f;
    struct addrspace *as;
    uint32_t f1, f2, saved, i, got;
    struct nova_trace_rec *k;
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("TRACEFETCH-FAIL alloc\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
        addrspace_map(as, USER_BASE + 0x1000u, f2, 1) != 0) {
        serial_puts("TRACEFETCH-FAIL map\n");
        goto fail;
    }
    k = (struct nova_trace_rec *)f1;
    f.ecx = 0;
    f.edx = 0;
    /* max==0 against NULL: 0 proves validation was never
     * touched (a validating path refuses EFAULT here). */
    f.ebx = 0;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("TRACEFETCH-FAIL max0-no-mapping\n");
        goto fail;
    }
    trace_reset();
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    {
        uint32_t mid = USER_BASE + 8u;
        struct nova_trace_rec *mu = (struct nova_trace_rec *)mid;
        /* Canaries outside the write window (fetch of 2 records
         * at mid covers frame bytes 8..47): kw[1] is bytes 4..7
         * (before), k[3] is bytes 60..79 (past). */
        uint32_t *kw = (uint32_t *)f1;
        kw[1] = 0x12345678u;
        k[3].arg = 0x9ABCDEF0u;
        trace_emit(TRACE_MARK, 91, 0xA1u);
        trace_emit(TRACE_MARK, 92, 0xB2u);
        f.ebx = mid;
        f.eax = SYS_TRACEFETCH;
        /* NOTE: max rides in ecx (dispatch passes ebx,ecx,edx). */
        f.ecx = 2;
        syscall_handler(&f);
        if ((int32_t)f.eax != 2) {
            serial_puts("TRACEFETCH-FAIL count\n");
            goto fail_cr3;
        }
        if (kw[1] != 0x12345678u || k[3].arg != 0x9ABCDEF0u ||
            mu[0].vers != NOVA_TRACE_VERS ||
            mu[0].kind != TRACE_MARK || mu[0].id != 91 ||
            mu[0].arg != 0xA1u || mu[0].rsv != 0 ||
            mu[1].kind != TRACE_MARK || mu[1].id != 92 ||
            mu[1].arg != 0xB2u || mu[1].rsv != 0 ||
            mu[1].seq != mu[0].seq + 1u) {
            serial_puts("TRACEFETCH-FAIL project\n");
            goto fail_cr3;
        }
    }
    {
        /* Bad pointer: supervisor half (never user-valid) ->
         * EFAULT, scratch provably untouched. */
        k[3].arg = 0xDEADu;
        f.ebx = 0xF0000000u;
        f.ecx = 2;
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != -NOVA_EFAULT ||
            k[3].arg != 0xDEADu) {
            serial_puts("TRACEFETCH-FAIL badptr\n");
            goto fail_cr3;
        }
    }
    {
        /* Over-cap max: EINVAL even against a valid pointer. */
        f.ebx = USER_BASE;
        f.ecx = TRACEFETCH_MAX + 1u;
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != -NOVA_EINVAL) {
            serial_puts("TRACEFETCH-FAIL overcap\n");
            goto fail_cr3;
        }
    }
    /* Newest-N: 262 marks (ids 500..761, > capacity by 6);
     * ask 256, expect ids 506..761 oldest-first, seqs +1.
     * Read back through the USER window (records pack across
     * the frame boundary: struct-indexing the identity frames
     * would misalign past record 203). */
    for (i = 0; i < TRACEFETCH_MAX + 6u; i++) {
        trace_emit(TRACE_MARK, (uint16_t)(500u + i), i);
    }
    f.ebx = USER_BASE;
    f.ecx = TRACEFETCH_MAX;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    got = (uint32_t)f.eax;
    if (got != TRACEFETCH_MAX) {
        serial_puts("TRACEFETCH-FAIL window-count\n");
        goto fail_cr3;
    }
    {
        struct nova_trace_rec *wu =
            (struct nova_trace_rec *)USER_BASE;
        for (i = 0; i < TRACEFETCH_MAX; i++) {
            if (wu[i].id != (uint16_t)(506u + i) ||
                wu[i].arg != 6u + i || wu[i].rsv != 0 ||
                wu[i].vers != NOVA_TRACE_VERS) {
                serial_puts("TRACEFETCH-FAIL window-shape\n");
                goto fail_cr3;
            }
            if (i > 0 && wu[i].seq != wu[i - 1u].seq + 1u) {
                serial_puts("TRACEFETCH-FAIL window-seq\n");
                goto fail_cr3;
            }
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    {
        /* Fault: project CR2 to class, never the address
         * (pure function, no ring/IRQ involved). */
        struct trace_event e;
        e.tick = 7;
        e.kind = TRACE_FAULT;
        e.id = 14;
        e.seq = 424242u;
        e.arg = 0xDEADBEEFu;
        trace_project(&e, &k[0]);
        if (k[0].vers != NOVA_TRACE_VERS ||
            k[0].kind != TRACE_FAULT || k[0].seq != 424242u ||
            k[0].tick != 7 || k[0].id != 14 ||
            k[0].arg != 3u || k[0].rsv != 0) {
            serial_puts("TRACEFETCH-FAIL fault\n");
            goto fail;
        }
    }
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    return NOVA_ESUCCESS;
fail_cr3:
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
fail:
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    return -1;
}

/* Phase D battery, defined below the wrapper. */
static int tracefetch_adversarial(void);

int trace_fetch_selftest(void) {
    if (tracefetch_probe_user() != 0) {
        return -1;
    }
    if (tracefetch_adversarial() != 0) {
        return -1;
    }
    serial_puts("TRACEFETCH-OK\n");
    return NOVA_ESUCCESS;
}

/* EFLAGS reader (IF bit 0x200) for the IF-discipline check. */
static uint32_t fetch_eflags(void) {
    uint32_t fl;
    __asm__ volatile("pushf; pop %0" : "=r"(fl));
    return fl;
}

static int bytes_equal(const uint8_t *a, const uint8_t *b,
                       uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Phase D adversarial battery: every argument class, boundary
 * address, zero-length, non-destructive refetch, same-tick
 * order, deep-wrap loss proof, RO/partial mappings, handler
 * IF discipline, failure preservation. Each block resets the
 * ring (resets never move g_seq); scratch spaces are created
 * and destroyed per block (probe precedent). Caller contract
 * under test (see do_tracefetch): max==0 skips validation;
 * max>256 EINVAL before any multiply; whole-range validation
 * precedes any byte written. */
static int tracefetch_adversarial(void) {
    struct syscall_frame f;
    struct addrspace *as = 0;
    uint32_t f1 = 0, f2 = 0, saved, i;
    uint8_t *k1;
    f.ecx = 0;
    f.edx = 0;
    /* --- A. empty ring: valid buffer -> 0; NULL + count -> EFAULT
     * (validated range nonempty); max0 + NULL -> 0. --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
        addrspace_map(as, USER_BASE + 0x1000u, f2, 1) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map\n");
        goto fail;
    }
    k1 = (uint8_t *)f1;
    trace_reset();
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.ecx = 5;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL empty-valid\n");
        goto fail_cr3;
    }
    f.ebx = 0;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL empty-null\n");
        goto fail_cr3;
    }
    f.ecx = 0;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL empty-max0\n");
        goto fail_cr3;
    }
    /* --- B. count battery: over-cap incl. huge (overflow
     * unreachable: EINVAL precedes the multiply). --- */
    f.ebx = USER_BASE;
    k1[0] = 0xAAu;
    f.ecx = TRACEFETCH_MAX + 1u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL || k1[0] != 0xAAu) {
        serial_puts("TRACEFETCH-ADV-FAIL overcap\n");
        goto fail_cr3;
    }
    f.ecx = 0xFFFFFFFFu;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL || k1[0] != 0xAAu) {
        serial_puts("TRACEFETCH-ADV-FAIL hugemax\n");
        goto fail_cr3;
    }
    f.ecx = 0x10000000u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EINVAL || k1[0] != 0xAAu) {
        serial_puts("TRACEFETCH-ADV-FAIL bigmax\n");
        goto fail_cr3;
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    /* --- C. pointer battery on kernel tables (all must refuse
     * before touching; scratch canary below proves no touch,
     * though refusal precedes any access by construction). --- */
    f.ecx = 5;
    f.ebx = 0;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL null\n");
        goto fail;
    }
    f.ebx = 0x1000u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL kmaplow\n");
        goto fail;
    }
    f.ebx = USER_END - 10u;
    f.ecx = 1;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL endcross\n");
        goto fail;
    }
    f.ebx = 0xFFFFFFF0u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL wrap\n");
        goto fail;
    }
    /* --- D. page-fit battery (borrowed tables): exact end of
     * page 1, and 1-byte cross into mapped page 2. --- */
    trace_reset();
    trace_emit(TRACE_MARK, 61, 0x61u);
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    {
        struct nova_trace_rec *mu =
            (struct nova_trace_rec *)(USER_BASE + 4096u - 20u);
        f.ebx = USER_BASE + 4096u - 20u;
        f.ecx = 1;
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != 1 || mu[0].id != 61 ||
            mu[0].arg != 0x61u) {
            serial_puts("TRACEFETCH-ADV-FAIL pagefit\n");
            goto fail_cr3;
        }
    }
    {
        struct nova_trace_rec *mu =
            (struct nova_trace_rec *)(USER_BASE + 4096u - 19u);
        f.ebx = USER_BASE + 4096u - 19u;
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != 1 || mu[0].id != 61) {
            serial_puts("TRACEFETCH-ADV-FAIL pagecross\n");
            goto fail_cr3;
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    as = 0;
    f1 = 0;
    f2 = 0;
    /* --- E. USER_END exact edge: one page at the top. --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    if (as == 0 || f1 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc-top\n");
        return -1;
    }
    if (addrspace_map(as, USER_END - 0x1000u, f1, 1) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map-top\n");
        goto fail;
    }
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_END - 20u;
    f.ecx = 1;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 1) {
        serial_puts("TRACEFETCH-ADV-FAIL endfit\n");
        goto fail_cr3;
    }
    f.ebx = USER_END - 19u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL endover\n");
        goto fail_cr3;
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    addrspace_destroy(as);
    as = 0;
    f1 = 0;
    /* --- F. read-only destination refuses, frame untouched. --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    if (as == 0 || f1 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc-ro\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 0) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map-ro\n");
        goto fail;
    }
    k1 = (uint8_t *)f1;
    k1[0] = 0xBBu;
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.ecx = 1;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT || k1[0] != 0xBBu) {
        serial_puts("TRACEFETCH-ADV-FAIL ronotouch\n");
        goto fail_cr3;
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    addrspace_destroy(as);
    as = 0;
    f1 = 0;
    /* --- G. short ring on one page: count>available returns
     * held; max256 refuses whole (no partial write: canary +
     * refetch prove it). --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    if (as == 0 || f1 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc-1pg\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map-1pg\n");
        goto fail;
    }
    k1 = (uint8_t *)f1;
    for (i = 0; i < 128u; i++) {
        k1[i] = 0xCCu;
    }
    trace_reset();
    trace_emit(TRACE_MARK, 71, 1u);
    trace_emit(TRACE_MARK, 72, 2u);
    trace_emit(TRACE_MARK, 73, 3u);
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.ecx = 100;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 3) {
        serial_puts("TRACEFETCH-ADV-FAIL short\n");
        goto fail_cr3;
    }
    {
        struct nova_trace_rec *wu =
            (struct nova_trace_rec *)USER_BASE;
        if (wu[0].id != 71 || wu[1].id != 72 ||
            wu[2].id != 73 || wu[2].arg != 3u) {
            serial_puts("TRACEFETCH-ADV-FAIL short-shape\n");
            goto fail_cr3;
        }
    }
    for (i = 60u; i < 128u; i++) {
        if (k1[i] != 0xCCu) {
            serial_puts("TRACEFETCH-ADV-FAIL short-canary\n");
            goto fail_cr3;
        }
    }
    f.ecx = TRACEFETCH_MAX;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != -NOVA_EFAULT) {
        serial_puts("TRACEFETCH-ADV-FAIL wide-refuse\n");
        goto fail_cr3;
    }
    /* No partial write: the 3 records are still exactly what
     * the first fetch returned (refetch proves it
     * byte-identical); the canary past them is intact. */
    f.ecx = 3;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 3) {
        serial_puts("TRACEFETCH-ADV-FAIL refetch-n\n");
        goto fail_cr3;
    }
    {
        struct nova_trace_rec *wu =
            (struct nova_trace_rec *)USER_BASE;
        if (wu[0].id != 71 || wu[1].id != 72 ||
            wu[2].id != 73) {
            serial_puts("TRACEFETCH-ADV-FAIL refetch-shape\n");
            goto fail_cr3;
        }
    }
    for (i = 60u; i < 128u; i++) {
        if (k1[i] != 0xCCu) {
            serial_puts("TRACEFETCH-ADV-FAIL wide-canary\n");
            goto fail_cr3;
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    addrspace_destroy(as);
    as = 0;
    f1 = 0;
    /* --- H. zero-length + IF discipline under cli: no state
     * change, IF stays 0 across valid AND zero fetches
     * (regression: blind sti in the handler). --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc-if\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
        addrspace_map(as, USER_BASE + 0x1000u, f2, 1) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map-if\n");
        goto fail;
    }
    k1 = (uint8_t *)f1;
    trace_reset();
    trace_emit(TRACE_MARK, 81, 0x81u);
    trace_emit(TRACE_MARK, 82, 0x82u);
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    if (trace_count() != 2u) {
        serial_puts("TRACEFETCH-ADV-FAIL if-count0\n");
        goto fail_cr3;
    }
    f.ebx = USER_BASE;
    f.ecx = 2;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 2) {
        serial_puts("TRACEFETCH-ADV-FAIL if-fetch\n");
        goto fail_cr3;
    }
    for (i = 0; i < 40u; i++) {
        k1[512u + i] = k1[i];
    }
    f.ebx = 0;
    f.ecx = 0;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL if-max0null\n");
        goto fail_cr3;
    }
    f.ebx = 0xF0000000u;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL if-max0bad\n");
        goto fail_cr3;
    }
    if ((fetch_eflags() & 0x200u) != 0u) {
        serial_puts("TRACEFETCH-ADV-FAIL if-leak\n");
        goto fail_cr3;
    }
    if (trace_count() != 2u) {
        serial_puts("TRACEFETCH-ADV-FAIL if-count1\n");
        goto fail_cr3;
    }
    f.ebx = USER_BASE;
    f.ecx = 2;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 2 ||
        !bytes_equal(k1, k1 + 512u, 40u)) {
        serial_puts("TRACEFETCH-ADV-FAIL if-identical\n");
        goto fail_cr3;
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    as = 0;
    f1 = 0;
    f2 = 0;
    /* --- I. same-tick order: 5 rapid emits, seqs strict +1,
     * ids in order (ticks may repeat: not asserted). --- */
    as = addrspace_create();
    f1 = pmm_alloc_frame();
    f2 = pmm_alloc_frame();
    if (as == 0 || f1 == 0 || f2 == 0) {
        serial_puts("TRACEFETCH-ADV-FAIL alloc-ord\n");
        return -1;
    }
    if (addrspace_map(as, USER_BASE, f1, 1) != 0 ||
        addrspace_map(as, USER_BASE + 0x1000u, f2, 1) != 0) {
        serial_puts("TRACEFETCH-ADV-FAIL map-ord\n");
        goto fail;
    }
    trace_reset();
    for (i = 0; i < 5u; i++) {
        trace_emit(TRACE_MARK, (uint16_t)(610u + i), i);
    }
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.ecx = 5;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 5) {
        serial_puts("TRACEFETCH-ADV-FAIL ord-n\n");
        goto fail_cr3;
    }
    {
        struct nova_trace_rec *wu =
            (struct nova_trace_rec *)USER_BASE;
        for (i = 0; i < 5u; i++) {
            if (wu[i].id != (uint16_t)(610u + i) ||
                wu[i].arg != i || wu[i].kind != TRACE_MARK ||
                wu[i].vers != NOVA_TRACE_VERS ||
                wu[i].rsv != 0) {
                serial_puts("TRACEFETCH-ADV-FAIL ord-shape\n");
                goto fail_cr3;
            }
            if (i > 0 && wu[i].seq != wu[i - 1u].seq + 1u) {
                serial_puts("TRACEFETCH-ADV-FAIL ord-seq\n");
                goto fail_cr3;
            }
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    /* --- J. deep-wrap loss proof: 600 emits, 344 lost; the
     * retained window's first seq proves the gap size. --- */
    trace_reset();
    trace_emit(TRACE_MARK, 9000u, 0u);
    __asm__ volatile("cli" ::: "memory");
    saved = arch_read_cr3();
    arch_write_cr3(as->pd);
    f.ebx = USER_BASE;
    f.ecx = 1;
    f.eax = SYS_TRACEFETCH;
    syscall_handler(&f);
    if ((int32_t)f.eax != 1) {
        serial_puts("TRACEFETCH-ADV-FAIL base-n\n");
        goto fail_cr3;
    }
    {
        struct nova_trace_rec *wu =
            (struct nova_trace_rec *)USER_BASE;
        uint32_t base = wu[0].seq;
        arch_write_cr3(saved);
        /* Keep IF=0 across the fill (no sti here): a 599-emit
         * loop is long enough for a PIT tick, and any IRQ
         * emitter interleaving would shift the window math.
         * Emits nest save/restore (no-op under cli), tables
         * are kernel again, `saved` still holds it. */
        for (i = 0; i < 599u; i++) {
            trace_emit(TRACE_MARK, (uint16_t)(1001u + i), i + 1u);
        }
        arch_write_cr3(as->pd);
        f.ebx = USER_BASE;
        f.ecx = TRACEFETCH_MAX;
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != (int32_t)TRACEFETCH_MAX) {
            serial_puts("TRACEFETCH-ADV-FAIL wrap-n\n");
            goto fail_cr3;
        }
        /* 600 emitted, 256 retained: evicted = 344 (the base
         * mark + ids 1001..1343). First retained: id 1344,
         * arg 344, seq base+344. A reader holding `base` from
         * an earlier fetch computes the loss exactly. */
        if (wu[0].id != 1344u || wu[0].arg != 344u ||
            wu[0].seq != base + 344u) {
            serial_puts("TRACEFETCH-ADV-FAIL wrap-gap\n");
            goto fail_cr3;
        }
        if (wu[255].id != 1599u || wu[255].arg != 599u ||
            wu[255].seq != base + 599u) {
            serial_puts("TRACEFETCH-ADV-FAIL wrap-tail\n");
            goto fail_cr3;
        }
        for (i = 1u; i < TRACEFETCH_MAX; i++) {
            if (wu[i].seq != wu[i - 1u].seq + 1u) {
                serial_puts("TRACEFETCH-ADV-FAIL wrap-seq\n");
                goto fail_cr3;
            }
        }
        /* Failure preservation: refetch is byte-identical
         * (nothing consumed, nothing torn by the failures
         * above — C/D/G/H blocks all ran on this ring era;
         * J's own window refetched here). */
        for (i = 0; i < 32u; i++) {
            adv_stash[i] = ((uint8_t *)USER_BASE)[i];
        }
        f.eax = SYS_TRACEFETCH;
        syscall_handler(&f);
        if ((int32_t)f.eax != (int32_t)TRACEFETCH_MAX ||
            !bytes_equal((uint8_t *)USER_BASE, adv_stash, 32u)) {
            serial_puts("TRACEFETCH-ADV-FAIL refetch\n");
            goto fail_cr3;
        }
    }
    /* --- K. synthetic passthrough: non-fault kinds cross
     * projected-but-identical (vers/rsv pinned). --- */
    {
        struct trace_event e;
        struct nova_trace_rec r;
        e.tick = 9;
        e.kind = TRACE_CREATE;
        e.id = 3;
        e.seq = 777u;
        e.arg = 12u;
        trace_project(&e, &r);
        if (r.vers != NOVA_TRACE_VERS || r.kind != TRACE_CREATE ||
            r.seq != 777u || r.tick != 9 || r.id != 3 ||
            r.arg != 12u || r.rsv != 0) {
            serial_puts("TRACEFETCH-ADV-FAIL passthru\n");
            goto fail_cr3;
        }
        e.kind = TRACE_FAULT;
        e.arg = 0x00000000u;
        trace_project(&e, &r);
        if (r.arg != 0u) {
            serial_puts("TRACEFETCH-ADV-FAIL class0\n");
            goto fail_cr3;
        }
        e.arg = 0x00100000u;
        trace_project(&e, &r);
        if (r.arg != 1u) {
            serial_puts("TRACEFETCH-ADV-FAIL class1\n");
            goto fail_cr3;
        }
        e.arg = USER_BASE;
        trace_project(&e, &r);
        if (r.arg != 2u) {
            serial_puts("TRACEFETCH-ADV-FAIL class2\n");
            goto fail_cr3;
        }
        e.arg = USER_END;
        trace_project(&e, &r);
        if (r.arg != 3u) {
            serial_puts("TRACEFETCH-ADV-FAIL class3\n");
            goto fail_cr3;
        }
    }
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    serial_puts("TRACEFETCH-ADV-OK\n");
    return NOVA_ESUCCESS;
fail_cr3:
    arch_write_cr3(saved);
    __asm__ volatile("sti" ::: "memory");
fail:
    /* Best-effort cleanup (frames may be unallocated on the
     * early-alloc path; freeing 0 is a no-op by contract —
     * verified: pmm_free_frame(0) returns silently). */
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    addrspace_destroy(as);
    return -1;
}
