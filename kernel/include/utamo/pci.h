/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PCI_H
#define UTAMO_PCI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTAMO_PCI_LIMIT 128u
struct pci_device {
    uint8_t bus, slot, function, class_code, subclass, interface, header_type;
    uint16_t vendor_id, device_id;
    uint32_t bars[6];
};
struct pci_inventory { struct pci_device devices[UTAMO_PCI_LIMIT]; size_t count; };
struct pci_ops {
    void *context;
    uint32_t (*read)(void *context, uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
};
struct pci_bar { uint64_t address, size; bool io, wide, prefetch; };
bool pci_scan(struct pci_inventory *out, const struct pci_ops *ops);
bool pci_decode_bar(const struct pci_device *device, unsigned int index, struct pci_bar *out);
bool pci_init(void);
size_t pci_count(void);
const struct pci_device *pci_get(size_t index);
uint32_t pci_read32(const struct pci_device *device, uint8_t offset);
/* Bootstrap exclusive claim only: temporarily disables decoding/mastering,
 * probes one BAR, restores BAR/command exactly. Never probes active OS drivers. */
bool pci_size_bar(const struct pci_device *device, unsigned int index, struct pci_bar *out);
bool pci_enable_mmio_dma(const struct pci_device *device);
void pci_list(void);
#endif
