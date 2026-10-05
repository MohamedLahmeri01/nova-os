/* NOVA OS libc: console I/O (Phase 6).
 * putchar/puts via sys_print; getchar spins on sys_getkey with yield
 * (no blocking primitive yet; hlt is privileged at CPL3, pause+YIELD
 * is the honest wait). No malloc (static buffers; brk comes later).
 */
#ifndef NOVA_LIBC_STDIO_H
#define NOVA_LIBC_STDIO_H

#include <stdint.h>

void nova_putchar(char c);
void nova_puts(const char *s);
int nova_getchar(void);

#endif /* NOVA_LIBC_STDIO_H */
