/* SPDX-License-Identifier: MIT */
#include <utamo/gdt.h>
static uint64_t gdt[UTAMO_GDT_ENTRIES] __attribute__((aligned(16)));
static struct tss64 tss;
static uint8_t emergency_stacks[3][16384] __attribute__((aligned(16)));
void gdt_init(void)
{
    tss = (struct tss64){0};
    for (size_t i = 0; i < 3u; ++i) {
        tss.ist[i] = (uint64_t)(uintptr_t)&emergency_stacks[i][16384];
    }
    /* No user mode, so RSP0 is unused. No IO bitmap => deny user port IO. */
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_build(gdt, (uint64_t)(uintptr_t)&tss);
    const struct descriptor_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };
    gdt_load(&pointer);
}
