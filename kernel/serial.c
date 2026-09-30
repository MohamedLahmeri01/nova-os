/* NOVA OS serial driver: COM1 16550 (Phase 1 prototype).
 * Port I/O goes through arch_io only.
 */
#if defined(__i386__)
#include "arch/x86/io.h"
#elif defined(__x86_64__)
#include "arch/x86_64/io.h"
#else
#error "serial: no arch_io backend for this architecture"
#endif

#include "serial.h"

#define COM1 0x3F8u

void serial_init(void) {
    arch_outb(COM1 + 1, 0x00); /* disable interrupts */
    arch_outb(COM1 + 3, 0x80); /* DLAB on */
    arch_outb(COM1 + 0, 0x01); /* divisor 1 = 115200 */
    arch_outb(COM1 + 1, 0x00);
    arch_outb(COM1 + 3, 0x03); /* 8N1, DLAB off */
    arch_outb(COM1 + 2, 0xC7); /* FIFO enable + clear, 14-byte threshold */
    arch_outb(COM1 + 4, 0x0B); /* RTS + DTR */
}

void serial_putc(char c) {
    while ((arch_inb(COM1 + 5) & 0x20) == 0) {
        /* wait for transmitter holding register empty */
    }
    arch_outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

static void puthex_nibble(uint8_t n) {
    serial_putc((char)(n < 10 ? '0' + n : 'a' + (n - 10)));
}

void serial_puthex32(uint32_t v) {
    for (int i = 7; i >= 0; i--) {
        puthex_nibble((uint8_t)((v >> (i * 4)) & 0xF));
    }
}

void serial_puthex64(uint64_t v) {
    serial_puthex32((uint32_t)(v >> 32));
    serial_puthex32((uint32_t)v);
}

void serial_putdec32(uint32_t v) {
    char buf[10];
    int n = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v > 0 && n < 10) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        serial_putc(buf[--n]);
    }
}
