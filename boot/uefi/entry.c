/* NOVA P3.3: UEFI to linked 64-bit kernel-entry handoff prototype.
 * NOT a standalone kernel binary, protected memory manager, or end-user OS.
 */
#include <stdint.h>
#include <stddef.h>
typedef uint64_t efi_status;
typedef void *efi_handle;
#define MS_ABI __attribute__((ms_abi))
#define EFI_SUCCESS UINT64_C(0)
#define EFI_LOAD_ERROR UINT64_C(0x8000000000000001)
#define EFI_INVALID_PARAMETER UINT64_C(0x8000000000000002)
#define EFI_SYSTEM_TABLE_SIGNATURE UINT64_C(0x5453595320494249)
#define MAP_BYTES 32768u
struct efi_table_header {uint64_t signature; uint32_t revision, header_size, crc32, reserved;};
struct efi_system_table {
    struct efi_table_header header;
    void *firmware_vendor;
    uint32_t firmware_revision, padding;
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
};
struct efi_boot_services {
    struct efi_table_header header;
    void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages;
    efi_status (MS_ABI *get_memory_map)(uint64_t *, void *, uint64_t *, uint64_t *, uint32_t *);
    void *allocate_pool, *free_pool;
    void *reserved_ops[19];
    efi_status (MS_ABI *exit_boot_services)(efi_handle, uint64_t);
};
_Static_assert(offsetof(struct efi_system_table, boot_services) == 96, "system table layout");
_Static_assert(offsetof(struct efi_boot_services, get_memory_map) == 56, "GetMemoryMap layout");
_Static_assert(offsetof(struct efi_boot_services, exit_boot_services) == 232, "ExitBootServices layout");

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void serial_init(void) {
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}
static void serial_write(const char *s) {
    for (; *s; ++s) {
        unsigned spins = 100000u;
        while (!(inb(0x3fd) & 0x20u) && --spins) {}
        if (!spins) return;
        outb(0x3f8, (uint8_t)*s);
    }
}
/* Loader-owned image storage must remain mapped after ExitBootServices.
 * P3.3 does not reclaim loader memory or set up its own page tables. */
static uint8_t map_data[MAP_BYTES] __attribute__((aligned(16)));
static uint8_t kernel_stack[32768] __attribute__((aligned(16)));
extern void MS_ABI nova_kernel_x64_entry(uint64_t map_bytes, uint64_t desc_size)
    __attribute__((noreturn));

static void MS_ABI transfer_to_kernel(uint64_t bytes, uint64_t stride)
    __attribute__((noreturn));
static void MS_ABI transfer_to_kernel(uint64_t bytes, uint64_t stride) {
    uintptr_t top = (uintptr_t)(kernel_stack + sizeof(kernel_stack));
    __asm__ volatile(
        "movq %0, %%rsp\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "movq %1, %%rcx\n\t"
        "movq %2, %%rdx\n\t"
        "call nova_kernel_x64_entry\n\t"
        "ud2\n\t"
        : : "r"(top), "r"(bytes), "r"(stride)
        : "rcx", "rdx", "memory");
    __builtin_unreachable();
}

efi_status MS_ABI efi_main(efi_handle image, struct efi_system_table *system) {
    serial_init();
    if (!system || system->header.signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        !system->boot_services) {
        serial_write("NOVA-UEFI-FAIL-SYSTEM-TABLE\r\n");
        return EFI_LOAD_ERROR;
    }
    struct efi_boot_services *boot = (struct efi_boot_services *)system->boot_services;
    if (!boot->get_memory_map || !boot->exit_boot_services) {
        serial_write("NOVA-UEFI-FAIL-BOOT-SERVICES\r\n");
        return EFI_LOAD_ERROR;
    }
    serial_write("NOVA-UEFI-X64-ENTRY-OK\r\n");
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        uint64_t bytes = sizeof(map_data), key = 0, stride = 0;
        uint32_t version = 0;
        efi_status status = boot->get_memory_map(&bytes, map_data, &key, &stride, &version);
        if (status != EFI_SUCCESS || !key || stride < 40u || stride > bytes ||
            bytes == 0u || bytes % stride != 0 || version != 1u) {
            serial_write("NOVA-UEFI-MAP-FAIL\r\n");
            return status == EFI_SUCCESS ? EFI_LOAD_ERROR : status;
        }
        /* No firmware calls or logging between map retrieval and exit. */
        status = boot->exit_boot_services(image, key);
        if (status == EFI_SUCCESS) transfer_to_kernel(bytes, stride);
        if (status != EFI_INVALID_PARAMETER) {
            serial_write("NOVA-UEFI-EXIT-FAIL\r\n");
            return status;
        }
    }
    serial_write("NOVA-UEFI-EXIT-RETRIES-EXHAUSTED\r\n");
    return EFI_LOAD_ERROR;
}
