/* NOVA OS P3.2: x86-64 UEFI smoke loader.
 * Diagnostic only: does not exit boot services, load the NOVA kernel,
 * or transfer control. COM1 access is QEMU/OVMF-specific, not portable UEFI.
 */
#include <stdint.h>
#include <stddef.h>

typedef uint64_t efi_status;
typedef void *efi_handle;
typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header;
typedef struct {
    efi_table_header header;
    void *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t _padding;
    efi_handle console_in_handle;
    void *console_in;
    efi_handle console_out_handle;
    void *console_out;
    efi_handle console_err_handle;
    void *console_err;
    void *runtime_services;
    void *boot_services;
    uint64_t config_count;
    void *config_table;
} efi_system_table;

#define EFI_SYSTEM_TABLE_SIGNATURE UINT64_C(0x5453595320494249)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_LOAD_ERROR (UINT64_C(0x8000000000000001))
_Static_assert(sizeof(efi_table_header) == 24, "EFI header size");
_Static_assert(offsetof(efi_system_table, boot_services) == 96,
               "EFI system table layout");

/* These x86 port I/O primitives are for emulator diagnostics only. */
static inline void io_out(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t io_in(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    io_out(0x3f9, 0x00); /* IER */
    io_out(0x3fb, 0x80); /* DLAB */
    io_out(0x3f8, 0x01); /* divisor low (115200) */
    io_out(0x3f9, 0x00); /* divisor high */
    io_out(0x3fb, 0x03); /* 8N1 */
    io_out(0x3fa, 0xc7); /* FIFO */
    io_out(0x3fc, 0x0b); /* OUT2, RTS, DTR */
}
static void serial_write(const char *s) {
    for (; *s != 0; s++) {
        /* Never wait indefinitely if the device is absent or broken. */
        unsigned spins = 100000u;
        while ((io_in(0x3fd) & 0x20u) == 0 && --spins != 0) {}
        if (spins == 0) return;
        io_out(0x3f8, (uint8_t)*s);
    }
}
/* x86-64 UEFI uses Microsoft x64 ABI; do not use SysV calling rules. */
efi_status __attribute__((ms_abi)) efi_main(efi_handle image,
                                           efi_system_table *system) {
    (void)image;
    serial_init();
    if (!system || system->header.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
        serial_write("NOVA-UEFI-FAIL-SYSTEM-TABLE\r\n");
        return EFI_LOAD_ERROR;
    }
    serial_write("NOVA-UEFI-X64-ENTRY-OK\r\n");
    serial_write("NOVA-UEFI-P3.2-SMOKE-ONLY\r\n");
    return EFI_SUCCESS;
}
