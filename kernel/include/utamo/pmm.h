/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PMM_H
#define UTAMO_PMM_H

#include <utamo/pmm_core.h>

/* Single-BSP wrappers preserve IF around each transaction. IRQ/NMI handlers
 * must not allocate. No SMP contract, heap, implicit zeroing or reclaiming.
 */
bool pmm_init(const struct memory_map *map, const struct pmm_plan *plan,
              void *storage);
bool pmm_alloc_page(uint64_t *out_phys);
bool pmm_alloc_pages(size_t count, uint64_t *out_phys);
bool pmm_free_page(uint64_t phys);
bool pmm_free_pages(uint64_t phys, size_t count);
bool pmm_reserve_range(uint64_t phys, size_t bytes);
bool pmm_pin_page(uint64_t phys);
bool pmm_is_allocated_page(uint64_t phys);
bool pmm_get_stats(struct pmm_stats *out_stats);

#endif
