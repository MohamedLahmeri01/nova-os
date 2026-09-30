/* NOVA OS kernel entry (Phase 0 prototype — NOT bootable yet).
 * Freestanding only: no libc, no CRT. The symbol `nova_entry` is the
 * documented future entry point; it currently parks the CPU.
 * The marker string lets the future VBox serial test detect boot.
 */
#include <stdint.h>

static const char nova_boot_marker[] = "NOVA-OS-BOOT-MARKER-v0";

__attribute__((noreturn)) void nova_entry(void) {
    /* Phase 1 will: validate boot info, init serial, print marker. */
    (void)nova_boot_marker;
    for (;;) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("hlt");
#else
        /* arch backends live in kernel/arch/; generic code never emits
         * privileged instructions outside those backends. */
#endif
    }
}
