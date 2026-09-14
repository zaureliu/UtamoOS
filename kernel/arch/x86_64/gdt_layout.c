/* SPDX-License-Identifier: MIT */
#include <utamo/gdt.h>
void gdt_build(uint64_t entries[UTAMO_GDT_ENTRIES], uint64_t tss_address)
{
    for (size_t i = 0; i < UTAMO_GDT_ENTRIES; ++i) {
        entries[i] = 0;
    }
    entries[1] = UTAMO_GDT_CODE;
    entries[2] = UTAMO_GDT_DATA;
    entries[3] = UTAMO_GDT_USER_CODE;
    entries[4] = UTAMO_GDT_USER_DATA;
    const uint64_t limit = sizeof(struct tss64) - 1u;
    entries[5] = limit | ((tss_address & UINT64_C(0xffffff)) << 16u) |
        (UINT64_C(0x89) << 40u) |
        (((tss_address >> 24u) & UINT64_C(0xff)) << 56u);
    entries[6] = tss_address >> 32u;
}
