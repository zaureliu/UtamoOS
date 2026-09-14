/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_HHDM_H
#define UTAMO_HHDM_H

#include <utamo/memory_map.h>

struct hhdm_context {
    const struct memory_map *map;
    uint64_t offset;
    uint64_t physical_mask;
    bool initialized;
};

/* Pure address arithmetic: no physical or virtual address is dereferenced.
 * Initialize context to zero once; successful initialization cannot be repeated.
 * The input map obeys memory_map_add's contract and remains immutable/alive.
 * Only base revision 3 mapped types are translated. The caller independently
 * verifies the live page tables implement the claimed direct mappings.
 * Any failure preserves destination context/output arguments.
 */
bool hhdm_type_mapped(enum memory_type type);
bool hhdm_init(struct hhdm_context *context, const struct memory_map *map,
               uint64_t offset, unsigned int physical_bits);
/* First fully containing region of any type. A request may not span separate
 * regions, even if they are adjacent and have the same mapped type.
 */
const struct memory_region *hhdm_region(const struct hhdm_context *context,
                                        uint64_t phys, size_t bytes);
bool hhdm_translate(const struct hhdm_context *context, uint64_t phys,
                    size_t bytes, uint64_t *out_virt);
bool hhdm_reverse(const struct hhdm_context *context, uint64_t virt,
                  size_t bytes, uint64_t *out_phys);

#endif
