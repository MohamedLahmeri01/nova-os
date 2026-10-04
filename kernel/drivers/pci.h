/* NOVA OS PCI interface (Phase 8b, enumeration).
 * Config-space access via 0xCF8/0xCFC (mechanism 1), recursive bus
 * walk through PCI-PCI bridges (bounded depth), known-device names
 * for the VBox guest hardware we target. All functions return 0 or
 * negative -errno; the selftest asserts OUR machine's devices.
 */
#ifndef NOVA_PCI_H
#define NOVA_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 32u

struct pci_dev {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar[6];
};

/* Enumerate all devices (up to PCI_MAX_DEVICES, rest counted but
 * dropped). Returns the count, or negative -errno. */
int pci_scan(void);
/* Devices found by the last scan (valid until the next scan). */
uint32_t pci_count(void);
const struct pci_dev *pci_get(uint32_t i);
/* Find first device by vendor/device (0 = not found). */
const struct pci_dev *pci_find(uint16_t vendor, uint16_t device);
/* Deterministic selftest: asserts the PIIX3 host bridge + IDE that
 * our constrained VM always has. Prints PCI-xx lines + PCI-OK.
 * Nonzero on failure (fatal). */
int pci_selftest(void);

#endif /* NOVA_PCI_H */
