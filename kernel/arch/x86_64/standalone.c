/* NOVA P3.5.1: inspect firmware memory map after ExitBootServices.
 * Validation only; no allocator, page table takeover, or freeing RAM.
 * UEFI descriptors remain in loader-owned mapped storage.
 */
#include <stdint.h>
#include <stddef.h>
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
static uint32_t read32(const uint8_t *p) {
    uint32_t value = 0;
    for (unsigned j=0;j<4;j++) value |= (uint32_t)p[j] << (8u*j);
    return value;
}
static uint64_t read64(const uint8_t *p) {
    uint64_t value = 0;
    for (unsigned j=0;j<8;j++) value |= (uint64_t)p[j] << (8u*j);
    return value;
}
static int validate_map(const void *raw, uint64_t bytes, uint64_t stride) {
    if (!raw || bytes == 0 || bytes > 32768 || stride < 40 ||
        stride > bytes || (bytes % stride) || bytes / stride > 512) return 0;
    const uint8_t *descs = (const uint8_t *)raw;
    uint64_t count = bytes / stride;
    for (uint64_t i=0;i<count;i++) {
        const uint8_t *d = descs+i*stride;
        uint32_t type = read32(d);
        uint64_t base = read64(d+8);
        uint64_t pages = read64(d+24);
        if (type > 15 || !pages || (base & 4095) ||
            pages > UINT64_MAX/4096) return 0;
        uint64_t size = pages*4096;
        if (base > UINT64_MAX-size) return 0;
        for (uint64_t j=0;j<i;j++) {
            const uint8_t *prev = descs+j*stride;
            uint64_t other = read64(prev+8);
            uint64_t other_pages = read64(prev+24);
            if (other_pages > UINT64_MAX/4096) return 0;
            uint64_t end = other + other_pages*4096;
            if (base < end && other < base+size) return 0;
        }
    }
    return 1;
}
/* SysV x86-64: RDI=map, RSI=size, RDX=stride. */
__attribute__((noreturn)) void nova_kernel_entry(
    const void *map, uint64_t bytes, uint64_t stride) {
    serial_write("NOVA-KERNEL-ELF64-ENTRY-OK\r\n");
    if (validate_map(map, bytes, stride))
        serial_write("NOVA-KERNEL-P3.5-MEMMAP-VALID-OK\r\n");
    else
        serial_write("NOVA-KERNEL-P3.5-MEMMAP-INVALID\r\n");
    __asm__ volatile("cli" ::: "memory");
    for (;;) __asm__ volatile("hlt" ::: "memory");
}
