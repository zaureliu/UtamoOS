/* SPDX-License-Identifier: MIT */
#include <utamo/pmm.h>
#include <utamo/cpu.h>

static struct pmm_state allocator;

bool pmm_init(const struct memory_map *map, const struct pmm_plan *plan,
              void *storage)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_init(&allocator, map, plan, storage);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_alloc_pages(size_t count, uint64_t *out_phys)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_alloc_pages(&allocator, count, out_phys);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_alloc_page(uint64_t *out_phys)
{
    return pmm_alloc_pages(1, out_phys);
}

bool pmm_free_pages(uint64_t phys, size_t count)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_free_pages(&allocator, phys, count);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_free_page(uint64_t phys)
{
    return pmm_free_pages(phys, 1);
}

bool pmm_reserve_range(uint64_t phys, size_t bytes)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_reserve_range(&allocator, phys, bytes);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_pin_page(uint64_t phys)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_pin_page(&allocator, phys);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_is_allocated_page(uint64_t phys)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_is_allocated_page(&allocator, phys);
    cpu_irq_restore(flags);
    return result;
}

bool pmm_get_stats(struct pmm_stats *out_stats)
{
    const uint64_t flags = cpu_irq_save();
    const bool result = pmm_core_get_stats(&allocator, out_stats);
    cpu_irq_restore(flags);
    return result;
}
