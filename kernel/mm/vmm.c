/* NOVA OS virtual memory manager: classic 32-bit paging (Phase 2b).
 *
 * Layout:
 *   [0x0, 0x1000)          present, read-only (null-WRITE guard; BIOS
 *                          data like the EBDA pointer at 0x40E stays
 *                          readable, null stores fault)
 *   [0x1000, 4MB)          4KB pages (first 4MB needs granularity for the
 *                          guards); stack-guard page absent
 *   [4MB, map_end)         4MB large pages, RW (needs PSE, gated below)
 *   [map_end, 4GB)         unmapped (user half + MMIO live here later)
 * map_end = arena end rounded up to 4MB. Tables (PD+PT) come from the PMM
 * itself; the map is identity, so all code keeps working across the
 * switch. NX is NOT available without PAE: XD enforcement slides to the
 * x86-64 long-mode milestone (see ADR-0011); the .text/.datax link split
 * is already in place for it.
 *
 * NOTE (ADR-0011): guest PAE PG-enable triple-faults deterministically
 * under VirtualBox 7.2 NEM on this host (9-boot elimination: NXE, XD,
 * PS pages, CR order, PSE, WP all ruled out; non-PAE works first try).
 * PAE stays out until re-proven under QEMU or long mode.
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "pmm.h"
#include "vmm.h"
#include "../../boot/mem_layout.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "vmm: no arch_cpu backend for this architecture"
#endif

#define PTE_P (1u << 0)
#define PTE_RW (1u << 1)
#define PTE_US (1u << 2)
#define PDE_PS (1u << 7)
#define PD_ENTRIES 1024u
#define PT_ENTRIES 1024u
#define LARGE_4MB (0x400000u)

static uint32_t *g_pd;
static uint32_t *g_pt0;
static uint32_t g_map_end;
static uint32_t vmm_scratch;

static void *alloc_zero_table(void) {
    uint32_t a = pmm_alloc_frame();
    if (a == 0) {
        nova_panic("vmm-no-table-frame");
    }
    uint32_t *p = (uint32_t *)(uintptr_t)a;
    for (uint32_t i = 0; i < 1024u; i++) {
        p[i] = 0;
    }
    return p;
}

int vmm_init(void) {
    uint32_t a, b, c, d;
    __asm__ volatile("cli");
    arch_cpuid(1u, &a, &b, &c, &d);
    if ((d & (1u << 3)) == 0) {
        nova_panic("vmm-no-pse");
    }
    uint32_t arena_end = pmm_arena_base() +
                         pmm_total_frames() * NOVA_FRAME_SIZE;
    g_map_end = (arena_end + LARGE_4MB - 1u) & ~(LARGE_4MB - 1u);
    serial_puts("VMM-STEP-TABLES\n");
    g_pd = alloc_zero_table();
    g_pt0 = alloc_zero_table();
    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        uint32_t page = i * 0x1000u;
        uint32_t e = page | PTE_P | PTE_RW;
        if (page == 0) {
            e = PTE_P; /* page zero: present read-only (null-write
                        * guard, BIOS data like EBDA@0x40E readable) */
        } else if (page == NOVA_STACK_GUARD) {
            e = 0; /* stack guard */
        }
        g_pt0[i] = e;
    }
    g_pd[0] = (uint32_t)(uintptr_t)g_pt0 | PTE_P | PTE_RW;
    for (uint32_t p = 1; p * LARGE_4MB < g_map_end; p++) {
        g_pd[p] = p * LARGE_4MB | PTE_P | PTE_RW | PDE_PS;
    }
    serial_puts("VMM-STEP-CR\n");
    /* Classic paging: PAE explicitly clear, PSE for 4MB pages. */
    arch_write_cr4((arch_read_cr4() | (1u << 4)) & ~(1u << 5));
    arch_write_cr3((uint32_t)(uintptr_t)g_pd);
    /* PG + WP (supervisor respects read-only pages). */
    arch_write_cr0(arch_read_cr0() | 0x80000000u | 0x00010000u);
    serial_puts("VMM-PG\n");
    serial_puts("VMM-CR3=0x");
    serial_puthex32(arch_read_cr3());
    serial_puts(" MAP-END=0x");
    serial_puthex32(g_map_end);
    serial_putc('\n');
    return 0;
}

void vmm_map_mmio(uint32_t phys) {    uint32_t p = phys & ~0xFFFu;
    uint32_t pd_idx = p >> 22;
    uint32_t pt_idx = (p >> 12) & 0x3FFu;
    if (pd_idx == 0) {
        nova_panic("vmm-mmio-low");
    }
    /* TEMP-EXP-LARGE: map the whole 4MB chunk as one large page (rules
     * out any PT-level bug; LAPIC lives at chunk base + 2MB). */
    g_pd[pd_idx] = (pd_idx * 0x400000u) | PTE_P | PTE_RW | PDE_PS |
                   (1u << 3) | (1u << 4); /* PWT|PCD */
    (void)pt_idx;
    arch_invlpg(p);
    arch_write_cr3(arch_read_cr3());
}

int mem_page_state(uint32_t addr, int *present, int *user,
                    int *writable) {
    uint32_t pde;
    /* Walk the CURRENT address space (syscalls run under the
     * caller's CR3); g_pd is only the kernel's own directory. */
    uint32_t *pd = (uint32_t *)(arch_read_cr3() & ~0xFFFu);
    if (present == 0 || user == 0 || writable == 0) {
        return -1;
    }
    pde = pd[addr >> 22];
    if (!(pde & PTE_P)) {
        *present = 0;
        *user = 0;
        *writable = 0;
        return 0;
    }
    if (pde & PDE_PS) {
        *present = 1;
        *user = 0; /* kernel large pages: supervisor-only */
        *writable = (pde & PTE_RW) != 0;
        return 0;
    }
    {
        uint32_t *pt = (uint32_t *)(pde & ~0xFFFu);
        uint32_t pte = pt[(addr >> 12) & 0x3FFu];
        *present = (pte & PTE_P) != 0;
        *user = 0;
        *writable = 0;
        if (*present) {
            *user = (pte & PTE_US) != 0;
            *writable = (pte & PTE_RW) != 0;
        }
        return 0;
    }
}

int vmm_selftest(void) {    if ((g_pt0[0] & (PTE_P | PTE_RW)) != PTE_P) {
        return -1; /* page zero must be present read-only */
    }
    if ((g_pt0[NOVA_KERNEL_LOAD >> 12] & PTE_P) == 0) {
        return -2; /* kernel text must be present */
    }
    if ((g_pt0[NOVA_STACK_GUARD >> 12] & PTE_P) != 0) {
        return -4; /* stack guard must be absent */
    }
    vmm_scratch = 0x12345678u; /* RW proof on a .bss word */
    if (vmm_scratch != 0x12345678u) {
        return -5;
    }
    vmm_scratch = 0;
    if ((arch_read_cr0() & 0x80000000u) == 0) {
        return -6; /* PG really on */
    }
    return 0;
}
