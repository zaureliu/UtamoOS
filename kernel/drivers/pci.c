/* SPDX-License-Identifier: MIT */
#include <utamo/pci.h>
#include <utamo/io.h>
#include <utamo/cpu.h>
#include <utamo/log.h>
static struct pci_inventory inventory;
static bool initialized;
static uint32_t address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    return UINT32_C(0x80000000) | ((uint32_t)bus << 16u) |
        ((uint32_t)slot << 11u) | ((uint32_t)function << 8u) | (offset & 0xfcu);
}
static uint32_t read_config(void *context, uint8_t bus, uint8_t slot,
                            uint8_t function, uint8_t offset)
{
    (void)context;
    const uint64_t flags = cpu_irq_save();
    io_out32(0xcf8u, address(bus, slot, function, offset));
    const uint32_t value = io_in32(0xcfcu);
    cpu_irq_restore(flags);
    return value;
}
uint32_t pci_read32(const struct pci_device *device, uint8_t offset)
{
    if (device == NULL || device->slot >= 32u || device->function >= 8u || (offset & 3u) != 0u) {
        return UINT32_MAX;
    }
    return read_config(NULL, device->bus, device->slot, device->function, offset);
}
static void write32(const struct pci_device *device, uint8_t offset, uint32_t value)
{
    io_out32(0xcf8u, address(device->bus, device->slot, device->function, offset));
    io_out32(0xcfcu, value);
}
static void command16(const struct pci_device *device, uint16_t command)
{
    io_out32(0xcf8u, address(device->bus, device->slot, device->function, 4u));
    io_out16(0xcfcu, command); /* Do not write RW1C PCI status bits. */
}
bool pci_size_bar(const struct pci_device *device, unsigned int index, struct pci_bar *out)
{
    struct pci_bar bar;
    if (out == NULL || !pci_decode_bar(device, index, &bar)) { return false; }
    const uint64_t flags = cpu_irq_save();
    const uint16_t command = (uint16_t)pci_read32(device, 4u);
    const uint8_t offset = (uint8_t)(16u + index * 4u);
    const uint32_t low = pci_read32(device, offset);
    const uint32_t high = bar.wide ? pci_read32(device, (uint8_t)(offset + 4u)) : 0u;
    command16(device, (uint16_t)(command & (uint16_t)~7u));
    write32(device, offset, UINT32_MAX);
    if (bar.wide) { write32(device, (uint8_t)(offset + 4u), UINT32_MAX); }
    uint64_t mask = pci_read32(device, offset) & (bar.io ? ~UINT32_C(3) : ~UINT32_C(15));
    if (bar.wide) { mask |= (uint64_t)pci_read32(device, (uint8_t)(offset + 4u)) << 32u; }
    write32(device, offset, low);
    if (bar.wide) { write32(device, (uint8_t)(offset + 4u), high); }
    command16(device, command);
    const bool restored = pci_read32(device, offset) == low &&
        (!bar.wide || pci_read32(device, (uint8_t)(offset + 4u)) == high) &&
        (uint16_t)pci_read32(device, 4u) == command;
    cpu_irq_restore(flags);
    bar.size = bar.wide ? ~mask + 1u : (uint64_t)(uint32_t)(~(uint32_t)mask + 1u);
    if (!restored || mask == 0u || bar.size == 0u ||
        (bar.size & (bar.size - 1u)) != 0u ||
        (bar.address & (bar.size - 1u)) != 0u || bar.size > UINT64_MAX - bar.address) {
        return false;
    }
    *out = bar;
    return true;
}
bool pci_enable_mmio_dma(const struct pci_device *device)
{
    if (device == NULL) { return false; }
    const uint64_t flags = cpu_irq_save();
    const uint16_t command = (uint16_t)pci_read32(device, 4u);
    command16(device, command | UINT16_C(0x406));
    const bool good = ((uint16_t)pci_read32(device, 4u) & 0x406u) == 0x406u;
    cpu_irq_restore(flags);
    return good;
}
bool pci_init(void)
{
    if (initialized) { return false; }
    const struct pci_ops ops = {.read = read_config};
    initialized = pci_scan(&inventory, &ops);
    return initialized;
}
size_t pci_count(void) { return initialized ? inventory.count : 0u; }
const struct pci_device *pci_get(size_t index)
{
    return initialized && index < inventory.count ? &inventory.devices[index] : NULL;
}
void pci_list(void)
{
    kprintf("PCI devices: %llu\n", (unsigned long long)pci_count());
    for (size_t i = 0u; i < pci_count(); ++i) {
        const struct pci_device *d = pci_get(i);
        kprintf("%u:%u.%u vendor=0x%x device=0x%x class=0x%x subclass=0x%x interface=0x%x\n",
            (unsigned int)d->bus, (unsigned int)d->slot, (unsigned int)d->function,
            (unsigned int)d->vendor_id, (unsigned int)d->device_id,
            (unsigned int)d->class_code, (unsigned int)d->subclass, (unsigned int)d->interface);
        for (unsigned int j = 0u; j < 6u; ++j) {
            struct pci_bar bar;
            if (pci_decode_bar(d, j, &bar)) {
                kprintf(" BAR%u %s base=0x%llx width=%u\n", j,
                    (const char *)(bar.io ? "IO" : "MMIO"), (unsigned long long)bar.address,
                    bar.wide ? 64u : 32u);
            }
        }
    }
}

bool pci_prepare_mmio(const struct pci_device *device)
{
    if (device == NULL) { return false; }
    const uint64_t flags = cpu_irq_save();
    const uint16_t command = (uint16_t)pci_read32(device, 4u);
    command16(device, (uint16_t)((command & (uint16_t)~4u) | 0x402u));
    const bool good = ((uint16_t)pci_read32(device, 4u) & 0x406u) == 0x402u;
    cpu_irq_restore(flags);
    return good;
}
