/* NOVA OS PCI enumeration (Phase 8b).
 * Mechanism 1 (0xCF8 address, 0xCFC data), dword-granular. Bus walk
 * recurses into PCI-PCI bridges (class 06/04, secondary bus from
 * offset 0x19), depth-capped. BAR sizing (write-all-ones) is NOT
 * done here (virtio phase needs addresses, not sizes; BIOS-assigned
 * BARs are read as-is). Single static table (no heap this early is
 * fine either way — heap exists, but static keeps bounds obvious).
 */
#include <stdint.h>

#include "serial.h"
#include "panic.h"
#include "drivers/pci.h"
#include "syscall/syscall.h"

#if defined(__i386__)
#include "../arch/x86/io.h"
#include "../arch/x86/cpu.h"
#elif defined(__x86_64__)
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#else
#error "pci: no arch backend for this architecture"
#endif

#define PCI_ADDR 0x0CF8u
#define PCI_DATA 0x0CFCu
#define PCI_MAX_BUS 256u
#define PCI_MAX_DEPTH 8u

static struct pci_dev g_devs[PCI_MAX_DEVICES];
static uint32_t g_count;
static uint32_t g_dropped;

static uint32_t pci_addr(uint32_t bus, uint32_t slot, uint32_t func,
                         uint32_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) | ((uint32_t)func << 8) |
           (off & 0xFCu);
}

static uint32_t pci_read(uint32_t bus, uint32_t slot, uint32_t func,
                         uint32_t off) {
    arch_outl(PCI_ADDR, pci_addr(bus, slot, func, off));
    return arch_inl(PCI_DATA);
}

static const char *dev_name(uint16_t vendor, uint16_t device) {
    if (vendor == 0x8086u && device == 0x1237u) {
        return "440FX";
    }
    if (vendor == 0x8086u && device == 0x7000u) {
        return "PIIX3-ISA";
    }
    if (vendor == 0x8086u && device == 0x7010u) {
        return "PIIX3-IDE";
    }
    if (vendor == 0x8086u && device == 0x7111u) {
        return "PIIX4-IDE";
    }
    if (vendor == 0x8086u && device == 0x7020u) {
        return "PIIX3-USB";
    }
    if (vendor == 0x8086u && device == 0x7113u) {
        return "PIIX4-PM";
    }
    if (vendor == 0x80EEu && device == 0xCAFEu) {
        return "VBox-GUEST";
    }
    if (vendor == 0x80EEu && device == 0xBEEFu) {
        return "VBox-VGA";
    }
    if (vendor == 0x8086u && device == 0x100Eu) {
        return "E1000";
    }
    if (vendor == 0x1AF4u && device >= 0x1000u && device <= 0x107Fu) {
        return "VIRTIO";
    }
    return "?";
}

static void scan_bus(uint32_t bus, uint32_t depth);

static void scan_func(uint32_t bus, uint32_t slot, uint32_t func,
                      uint32_t depth) {
    uint32_t id = pci_read(bus, slot, func, 0);
    uint32_t cls;
    struct pci_dev *d;
    if ((id & 0xFFFFu) == 0xFFFFu) {
        return; /* no device */
    }
    if (g_count < PCI_MAX_DEVICES) {
        d = &g_devs[g_count];
        d->bus = (uint8_t)bus;
        d->slot = (uint8_t)slot;
        d->func = (uint8_t)func;
        d->vendor = (uint16_t)(id & 0xFFFFu);
        d->device = (uint16_t)(id >> 16);
        cls = pci_read(bus, slot, func, 8);
        d->class_code = (uint8_t)(cls >> 24);
        d->subclass = (uint8_t)(cls >> 16);
        d->prog_if = (uint8_t)(cls >> 8);
        for (uint32_t b = 0; b < 6u; b++) {
            d->bar[b] = pci_read(bus, slot, func, 16 + b * 4u);
        }
        g_count++;
    } else {
        g_dropped++;
    }
    /* Bridge? Recurse into the secondary bus (depth-capped). */
    cls = pci_read(bus, slot, func, 8);
    if (((cls >> 24) & 0xFFu) == 0x06u &&
        (((cls >> 16) & 0xFFu) == 0x04u) && depth < PCI_MAX_DEPTH) {
        uint32_t sec = pci_read(bus, slot, func, 24);
        uint32_t sub = (sec >> 8) & 0xFFu;
        if (sub != bus) {
            scan_bus(sub, depth + 1u);
        }
    }
}

static void scan_bus(uint32_t bus, uint32_t depth) {
    for (uint32_t slot = 0; slot < 32u; slot++) {
        uint32_t id = pci_read(bus, slot, 0, 0);
        uint32_t hdr;
        if ((id & 0xFFFFu) == 0xFFFFu) {
            continue; /* empty slot (no func 0) */
        }
        hdr = pci_read(bus, slot, 0, 12);
        if ((hdr & 0x00800000u) != 0) {
            /* Multifunction: probe all 8. */
            for (uint32_t f = 0; f < 8u; f++) {
                scan_func(bus, slot, f, depth);
            }
        } else {
            scan_func(bus, slot, 0, depth);
        }
    }
}

int pci_scan(void) {
    g_count = 0;
    g_dropped = 0;
    scan_bus(0, 0);
    return (int)g_count;
}

uint32_t pci_count(void) {
    return g_count;
}

const struct pci_dev *pci_get(uint32_t i) {
    if (i >= g_count) {
        return 0;
    }
    return &g_devs[i];
}

const struct pci_dev *pci_find(uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; i < g_count; i++) {
        if (g_devs[i].vendor == vendor && g_devs[i].device == device) {
            return &g_devs[i];
        }
    }
    return 0;
}

static void puthex_n(uint32_t v, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        uint32_t d = (v >> (i * 4u)) & 0xFu;
        serial_putc((char)(d < 10u ? '0' + d : 'a' + d - 10u));
    }
}

int pci_selftest(void) {
    int n = pci_scan();
    if (n < 0) {
        serial_puts("PCI-FAIL scan\n");
        return -1;
    }
    for (uint32_t i = 0; i < g_count; i++) {
        const struct pci_dev *d = &g_devs[i];
        serial_puts("PCI ");
        puthex_n(d->bus, 2);
        serial_putc(':');
        puthex_n(d->slot, 2);
        serial_putc('.');
        puthex_n(d->func, 1);
        serial_putc(' ');
        puthex_n(d->vendor, 4);
        serial_putc(':');
        puthex_n(d->device, 4);
        serial_putc(' ');
        puthex_n(d->class_code, 2);
        serial_putc('/');
        puthex_n(d->subclass, 2);
        serial_putc(' ');
        serial_puts(dev_name(d->vendor, d->device));
        serial_putc('\n');
    }
    /* Our machine, our proof: 440FX host bridge + an IDE function
     * (PIIX3 7010 or PIIX4 7111 depending on chipset) are always on
     * bus 0 of the constrained VM (like the NVSC tag: proves we
     * enumerated REAL hardware, not echoes). */
    if (pci_find(0x8086u, 0x1237u) == 0) {
        serial_puts("PCI-FAIL host-bridge\n");
        return -1;
    }
    if (pci_find(0x8086u, 0x7010u) == 0 &&
        pci_find(0x8086u, 0x7111u) == 0) {
        serial_puts("PCI-FAIL ide\n");
        return -1;
    }
    if (g_dropped != 0) {
        serial_puts("PCI-FAIL dropped\n");
        return -1;
    }
    serial_puts("PCI-OK\n");
    return NOVA_ESUCCESS;
}
