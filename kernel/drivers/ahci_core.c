/* SPDX-License-Identifier: MIT */
#include <utamo/ahci.h>
#include <utamo/string.h>
static void put32(unsigned char *p, uint32_t value)
{
    for (size_t i = 0u; i < 4u; ++i) { p[i] = (unsigned char)(value >> (i * 8u)); }
}
static uint16_t word(const unsigned char *data, size_t index)
{
    return (uint16_t)((uint16_t)data[index * 2u] | (uint16_t)((uint16_t)data[index * 2u + 1u] << 8u));
}
bool ahci_build_read(void *header, void *table, uint64_t table_phys,
                     uint64_t buffer_phys, uint8_t command, uint64_t lba, size_t count)
{
    if (header == NULL || table == NULL || (table_phys & 127u) != 0u ||
        (buffer_phys & 1u) != 0u || buffer_phys > UINT64_MAX - 4095u ||
        count == 0u || count > 8u || lba >= (UINT64_C(1) << 48u) ||
        count > (UINT64_C(1) << 48u) - lba ||
        (command != 0xecu && command != 0x25u) ||
        (command == 0xecu && (lba != 0u || count != 1u))) { return false; }
    unsigned char *h = header, *t = table;
    memset(h, 0, 32u);
    memset(t, 0, 144u);
    put32(h, UINT32_C(0x10005)); /* CFL=5, W=0, PRDTL=1. */
    put32(h + 8u, (uint32_t)table_phys);
    put32(h + 12u, (uint32_t)(table_phys >> 32u));
    t[0] = 0x27u; t[1] = 0x80u; t[2] = command;
    if (command == 0x25u) {
        t[7] = 0x40u;
        for (size_t i = 0u; i < 6u; ++i) {
            t[i < 3u ? 4u + i : 5u + i] = (unsigned char)(lba >> (i * 8u));
        }
        t[12] = (unsigned char)count;
    }
    put32(t + 128u, (uint32_t)buffer_phys);
    put32(t + 132u, (uint32_t)(buffer_phys >> 32u));
    put32(t + 140u, (uint32_t)(count * 512u - 1u));
    return true;
}
bool ahci_identify(const unsigned char data[512], uint64_t *sectors)
{
    if (data == NULL || sectors == NULL || (word(data, 0u) & 0x8000u) != 0u ||
        (word(data, 83u) & 0xc400u) != 0x4400u) { return false; }
    const uint16_t sizes = word(data, 106u);
    if ((sizes & 0xc000u) == 0x4000u && (sizes & 0x1000u) != 0u &&
        ((uint32_t)word(data, 117u) | ((uint32_t)word(data, 118u) << 16u)) != 256u) {
        return false;
    }
    uint64_t capacity = 0u;
    for (size_t i = 0u; i < 4u; ++i) { capacity |= (uint64_t)word(data, 100u + i) << (i * 16u); }
    if (capacity == 0u || capacity > (UINT64_C(1) << 48u)) { return false; }
    *sectors = capacity;
    return true;
}
