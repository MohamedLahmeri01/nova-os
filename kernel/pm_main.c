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
#include "irq/irq.h"
#include "time/time.h"
#include "sched/sched.h"
#include "../boot/boot_info.h"

void nova_pm_main(const nova_boot_info_t *info) {
    serial_puts("NOVA-OS-BOOT-MARKER-v0\n");
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
    irq_init();
    timer_init();
    {
        int sched_rc = sched_selftest();
        if (sched_rc != 0) {
            serial_puts("SCHED-SELFTEST-FAIL code=");
            serial_putdec32((uint32_t)sched_rc);
            serial_putc('\n');
            nova_panic("sched-selftest-fail");
        }
    }
#ifdef NOVA_FAULT_TEST
    serial_puts("INJECT-FAULT\n");
    __asm__ volatile("ud2"); /* deliberate #UD: proves IDT->trap->panic */
    nova_panic("fault-test-unreached");
#else
    serial_puts("NOVA-OS-READY-HALT\n");
#endif
}
