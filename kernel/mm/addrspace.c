/* NOVA OS address spaces (Phase 4b).
 * Clone shares kernel frames (identity low map); user PTs are private
 * and freed on destroy (user DATA frames are the caller's; only tables
 * are owned here). map_page refuses the kernel half outright.
 */
#include <stdint.h>
#include <stddef.h>

#include "serial.h"
#include "panic.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/addrspace.h"

#if defined(__i386__)
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/cpu.h"
#else
#error "addrspace: no arch_cpu backend for this architecture"
#endif

#define PTE_P (1u << 0)
#define PTE_RW (1u << 1)
#define PTE_US (1u << 2)
#define KERNEL_PDS 256u /* PD entries cloned (identity low 1GB) */

struct addrspace *addrspace_create(void) {
    struct addrspace *as;
    uint32_t pd;
    uint32_t *kpd;
    uint32_t *npd;
    as = (struct addrspace *)kmalloc(sizeof(struct addrspace));
    if (as == 0) {
        return 0;
    }
    pd = pmm_alloc_frame();
    if (pd == 0) {
        kfree(as);
        return 0;
    }
    kpd = (uint32_t *)arch_read_cr3();
    npd = (uint32_t *)pd;
    /* Zero the user half explicitly: PD frames are recycled (never
     * zeroed by PMM), and stale user PDEs would resurrect freed PTs
     * (use-after-free across address spaces). */
    for (uint32_t i = 0; i < 1024u; i++) {
        npd[i] = (i < KERNEL_PDS) ? kpd[i] : 0;
    }
    as->pd = pd;
    return as;
}

void addrspace_destroy(struct addrspace *as) {
    uint32_t *pd;
    if (as == 0) {
        return;
    }
    pd = (uint32_t *)as->pd;
    for (uint32_t i = KERNEL_PDS; i < 1024u; i++) {
        if ((pd[i] & PTE_P) && !(pd[i] & (1u << 7))) {
            pmm_free_frame(pd[i] & ~0xFFFu);
        }
    }
    pmm_free_frame(as->pd);
    kfree(as);
}

void addrspace_map(struct addrspace *as, uint32_t virt, uint32_t phys,
                   int writable) {
    uint32_t pdi;
    uint32_t *pt;
    if (as == 0 || virt < USER_BASE || (virt & 0xFFFu) != 0 ||
        (phys & 0xFFFu) != 0) {
        nova_panic("addrspace-bad-map");
    }
    pdi = virt >> 22;
    {
        uint32_t *pd = (uint32_t *)as->pd;
        if ((pd[pdi] & PTE_P) == 0) {
            uint32_t ptp = pmm_alloc_frame();
            uint32_t *npt;
            if (ptp == 0) {
                nova_panic("addrspace-no-pt");
            }
            npt = (uint32_t *)ptp;
            for (uint32_t i = 0; i < 1024u; i++) {
                npt[i] = 0;
            }
            pd[pdi] = ptp | PTE_P | PTE_RW | PTE_US;
        }
        pt = (uint32_t *)(pd[pdi] & ~0xFFFu);
    }
    pt[(virt >> 12) & 0x3FFu] =
        phys | PTE_P | PTE_US | (writable ? PTE_RW : 0u);
}
