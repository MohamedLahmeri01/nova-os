/* NOVA OS hi: second user program (Phase 7b exec proof + `run` target).
 * Linked at 0x80000000 like init (own binary, own blob). Prints HI-OK
 * with its pid, then exits. Seeded into ramfs as /bin/hi at boot.
 */
#include <stdint.h>

#include "nova.h"
#include "stdio.h"

static void hi_putdec(uint32_t v) {
    char buf[10];
    int n = 0;
    if (v == 0) {
        nova_putchar('0');
        return;
    }
    while (v > 0 && n < 10) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        n--;
        nova_putchar(buf[n]);
    }
}

__attribute__((section(".text.startup"))) void ustart(void) {
    int32_t pid = nova_getpid();
    nova_print("HI-OK pid=", 10);
    hi_putdec((uint32_t)pid);
    nova_putchar('\n');
    nova_exit(0);
}
