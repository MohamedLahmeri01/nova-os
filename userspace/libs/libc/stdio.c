/* NOVA OS libc: console I/O (Phase 6). */
#include <stdint.h>

#include "nova.h"
#include "stdio.h"

void nova_putchar(char c) {
    nova_print(&c, 1);
}

void nova_puts(const char *s) {
    uint32_t n = 0;
    while (s[n] != 0) {
        n++;
    }
    if (n > 0) {
        nova_print(s, n);
    }
    nova_putchar('\n');
}

int nova_getchar(void) {
    for (;;) {
        int32_t k = nova_getkey();
        if (k >= 0) {
            return (int)(k & 0xFF);
        }
        nova_yield();
    }
}
