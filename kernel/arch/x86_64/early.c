/* NOVA P3.3: linked x86-64 early kernel entry.
 * No independent kernel binary, MMU takeover, IDT or user processes yet.
 */
#include <stdint.h>
#define MS_ABI __attribute__((ms_abi))
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_write(const char *s) {
    for (; *s; ++s) {
        unsigned spins = 100000u;
        while (!(inb(0x3fd) & 0x20u) && --spins) {}
        if (!spins) return;
        outb(0x3f8, (uint8_t)*s);
    }
}
void MS_ABI nova_kernel_x64_entry(uint64_t map_bytes, uint64_t desc_size)
    __attribute__((noreturn));
void MS_ABI nova_kernel_x64_entry(uint64_t map_bytes, uint64_t desc_size) {
    if (!map_bytes || desc_size < 40u || map_bytes % desc_size)
        serial_write("NOVA-KERNEL-X64-BAD-HANDOFF\r\n");
    else
        serial_write("NOVA-KERNEL-X64-ENTRY-OK\r\n");
    __asm__ volatile("cli" ::: "memory");
    for (;;) __asm__ volatile("hlt" ::: "memory");
}
