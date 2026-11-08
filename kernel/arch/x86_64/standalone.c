/* NOVA P3.4: independent, position-independent ELF64 kernel entry.
 * Experimental emulator diagnostic, not a production hardware driver.
 */
#include <stdint.h>
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_write(const char *message) {
    while (*message) {
        unsigned spins = 100000u;
        while (!(inb(0x3fd) & 0x20u) && --spins) {}
        if (!spins) return;
        outb(0x3f8, (uint8_t)*message++);
    }
}
__attribute__((noreturn)) void nova_kernel_entry(void) {
    serial_write("NOVA-KERNEL-ELF64-ENTRY-OK\r\n");
    __asm__ volatile("cli" ::: "memory");
    for (;;) __asm__ volatile("hlt" ::: "memory");
}
