/* NOVA OS uname: SDK sample program (Phase 11a).
 * Built only against sdk/include (nova/nova.h, nova/stdio.h): the
 * reference third user program. Prints SDK-OK with ABI version,
 * pid, and uid, then exits. Seeded into ramfs as /bin/uname at
 * boot and spawned after hi; the marker gates the SDK build end
 * to end (headers + link script + manifest all exercised).
 */
#include <stdint.h>

#include "nova/nova.h"
#include "nova/stdio.h"

#ifndef NOVA_VERSION
#define NOVA_VERSION "dev"
#endif

static void uputdec(uint32_t v) {
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
    int32_t abi = nova_version();
    int32_t pid = nova_getpid();
    int32_t uid = nova_cred(CRED_OP_GETUID, 0);
    nova_print("SDK-OK abi=", 11);
    uputdec((uint32_t)abi);
    nova_print(" pid=", 5);
    uputdec((uint32_t)pid);
    nova_print(" uid=", 5);
    uputdec((uint32_t)uid);
    nova_print(" ver=", 5);
    nova_puts(NOVA_VERSION);
    nova_exit(0);
}
