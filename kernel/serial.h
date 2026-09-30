/* NOVA OS serial driver interface (Phase 1 prototype).
 * 16550 UART on COM1, 115200 8N1. Panic/log path will build on this.
 */
#ifndef NOVA_SERIAL_H
#define NOVA_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_puthex32(uint32_t v);
void serial_puthex64(uint64_t v);
void serial_putdec32(uint32_t v);

#endif /* NOVA_SERIAL_H */
