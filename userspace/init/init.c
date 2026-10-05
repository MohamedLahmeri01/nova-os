/* NOVA OS init: first user program (Phase 6).
 * Linked at 0x80000000 via sdk/user.ld (.text.startup first for a
 * deterministic entry). Prints a banner, then runs the shell.
 */
#include <stdint.h>

#include "nova/nova.h"
#include "nova/stdio.h"
#include "sh.h"

#ifndef NOVA_VERSION
#define NOVA_VERSION "dev"
#endif

__attribute__((section(".text.startup"))) void ustart(void) {
    nova_puts("NOVA OS " NOVA_VERSION " init");
    sh_main();
    nova_puts("shell exited; halting");
    for (;;) {
        nova_yield();
    }
}
