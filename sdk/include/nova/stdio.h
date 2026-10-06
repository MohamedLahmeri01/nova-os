/* NOVA OS libc: console I/O (Phase 6, blocking getkey 16-i).
 * putchar/puts via sys_print; getchar loops on sys_getkey (the
 * kernel parks idle readers till the IRQ push wakes them, so the
 * yield below only spins pre-scheduler or under contention).
 * No malloc (static buffers; brk comes later).
 */
#ifndef NOVA_LIBC_STDIO_H
#define NOVA_LIBC_STDIO_H

#include <stdint.h>

void nova_putchar(char c);
void nova_puts(const char *s);
int nova_getchar(void);

#endif /* NOVA_LIBC_STDIO_H */
