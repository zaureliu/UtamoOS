/* SPDX-License-Identifier: MIT */
#include <utamo/pci.h>
#include <utamo/string.h>
bool pci_scan(struct pci_inventory *out, const struct pci_ops *ops)
{
    if (out == NULL || ops == NULL || ops->read == NULL || out->count != 0u) {
        return false;
    }
    for (unsigned int bus = 0u; bus < 256u; ++bus) {
        for (unsigned int slot = 0u; slot < 32u; ++slot) {
            const uint32_t first = ops->read(ops->context, (uint8_t)bus, (uint8_t)slot, 0u, 0u);
            if ((first & 0xffffu) == 0xffffu) {
                continue;
            }
            const uint32_t header = ops->read(ops->context, (uint8_t)bus, (uint8_t)slot, 0u, 12u);
            const unsigned int functions = (header & 0x800000u) != 0u ? 8u : 1u;
            for (unsigned int function = 0u; function < functions; ++function) {
                const uint32_t id = ops->read(ops->context, (uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0u);
                if ((id & 0xffffu) == 0xffffu) { continue; }
                if (out->count == UTAMO_PCI_LIMIT) {
                    memset(out, 0, sizeof(*out));
                    return false;
                }
                const uint32_t cls = ops->read(ops->context, (uint8_t)bus, (uint8_t)slot, (uint8_t)function, 8u);
                const uint32_t hdr = ops->read(ops->context, (uint8_t)bus, (uint8_t)slot, (uint8_t)function, 12u);
                struct pci_device *device = &out->devices[out->count++];
                *device = (struct pci_device){
                    .bus = (uint8_t)bus, .slot = (uint8_t)slot, .function = (uint8_t)function,
                    .vendor_id = (uint16_t)id, .device_id = (uint16_t)(id >> 16u),
                    .class_code = (uint8_t)(cls >> 24u), .subclass = (uint8_t)(cls >> 16u),
                    .interface = (uint8_t)(cls >> 8u), .header_type = (uint8_t)(hdr >> 16u)
                };
                const unsigned int type = device->header_type & 0x7fu;
                const unsigned int bars = type == 0u ? 6u : type == 1u ? 2u : 0u;
                for (unsigned int bar = 0u; bar < bars; ++bar) {
                    device->bars[bar] = ops->read(ops->context, (uint8_t)bus,
                        (uint8_t)slot, (uint8_t)function, (uint8_t)(16u + bar * 4u));
                }
            }
        }
    }
    return true;
}
bool pci_decode_bar(const struct pci_device *device, unsigned int index, struct pci_bar *out)
{
    if (device == NULL || out == NULL) { return false; }
    const unsigned int type = device->header_type & 0x7fu;
    const unsigned int count = type == 0u ? 6u : type == 1u ? 2u : 0u;
    if (index >= count) { return false; }
    /* An upper dword of a 64-bit BAR is not an independent resource. */
    for (unsigned int i = 0u; i < index; ++i) {
        if ((device->bars[i] & 7u) == 4u && ++i == index) { return false; }
    }
    const uint32_t value = device->bars[index];
    struct pci_bar bar = {.io = (value & 1u) != 0u};
    if (value == 0u || value == UINT32_MAX) { return false; }
    if (bar.io) {
        bar.address = value & ~UINT32_C(3);
    } else {
        const uint32_t memory_type = (value >> 1u) & 3u;
        if (memory_type != 0u && memory_type != 2u) { return false; }
        bar.wide = memory_type == 2u;
        bar.prefetch = (value & 8u) != 0u;
        bar.address = value & ~UINT32_C(15);
        if (bar.wide) {
            if (index + 1u >= count) { return false; }
            bar.address |= (uint64_t)device->bars[index + 1u] << 32u;
        }
    }
    if (bar.address == 0u) { return false; }
    *out = bar;
    return true;
}
