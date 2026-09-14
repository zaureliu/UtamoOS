/* SPDX-License-Identifier: MIT */
#include <utamo/memory.h>
bool memory_align_up(uint64_t value, uint64_t *out)
{
    if (out == NULL || value > UINT64_MAX - (MEMORY_PAGE_SIZE - 1u)) {
        return false;
    }
    *out = (value + MEMORY_PAGE_SIZE - 1u) & ~(MEMORY_PAGE_SIZE - 1u);
    return true;
}
uint64_t memory_align_down(uint64_t value)
{
    return value & ~(MEMORY_PAGE_SIZE - 1u);
}
bool memory_is_page_aligned(uint64_t value)
{
    return (value & (MEMORY_PAGE_SIZE - 1u)) == 0u;
}
bool memory_is_canonical(uint64_t address)
{
    return address <= UINT64_C(0x00007fffffffffff) ||
           address >= UINT64_C(0xffff800000000000);
}
bool memory_physical_mask(unsigned int bits, uint64_t *out)
{
    if (out == NULL || bits < 32u || bits > 52u) {
        return false;
    }
    *out = ((UINT64_C(1) << bits) - 1u) & ~(MEMORY_PAGE_SIZE - 1u);
    return true;
}
unsigned int memory_page_index(uint64_t virt, unsigned int level)
{
    if (level < 1u || level > 4u) {
        return 512u;
    }
    return (unsigned int)((virt >> (12u + 9u * (level - 1u))) & UINT64_C(511));
}
