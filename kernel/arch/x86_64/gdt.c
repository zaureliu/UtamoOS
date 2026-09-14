/* SPDX-License-Identifier: MIT */
#include <utamo/gdt.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
static uint64_t gdt[UTAMO_GDT_ENTRIES] __attribute__((aligned(16)));
static struct tss64 tss;
static bool initialized;
static uint8_t emergency_stacks[3][16384] __attribute__((aligned(16)));
void gdt_init(void)
{
    tss = (struct tss64){0};
    for (size_t i = 0; i < 3u; ++i) {
        tss.ist[i] = (uint64_t)(uintptr_t)&emergency_stacks[i][16384];
    }
    /* RSP0 is set by the scheduler before CPL3 entry. No bitmap denies IO. */
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_build(gdt, (uint64_t)(uintptr_t)&tss);
    const struct descriptor_pointer pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };
    gdt_load(&pointer);
    initialized = true;
}

bool gdt_set_rsp0(uint64_t stack_top)
{
    const uint64_t saved = cpu_irq_save();
    const bool valid = initialized && (saved & UINT64_C(0x200)) == 0u &&
        memory_is_canonical(stack_top) &&
        stack_top > UINT64_C(0xffff800000000000) &&
        (stack_top & UINT64_C(15)) == 0u;
    if (valid) {
        /* Hardware reads this field; NMI/DF/MC use their unchanged ISTs. */
        ((volatile struct tss64 *)&tss)->rsp[0] = stack_top;
    }
    cpu_irq_restore(saved);
    return valid;
}
