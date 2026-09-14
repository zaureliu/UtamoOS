/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_VMM_CORE_H
#define UTAMO_VMM_CORE_H
#include <utamo/vmm.h>
/* Caller supplies owned, stable table storage and serialization.
 * alloc returns a distinct aligned frame; table returns NULL for unsafe access.
 * free and commit cannot fail. commit transfers published-table ownership:
 * kernel tables are pinned; private spaces retain tables until destruction.
 * IRQ code must not access the allocator or mutate the page tables. */
struct vmm_ops {
    void *context;
    uint64_t *(*table)(void *context, uint64_t phys);
    bool (*alloc)(void *context, uint64_t *phys);
    void (*free)(void *context, uint64_t phys);
    void (*commit)(void *context, uint64_t phys);
    void (*invalidate)(void *context, uint64_t virt);
};
struct vmm_space {
    uint64_t root_phys, physical_mask;
    bool nx_enabled, initialized;
    struct vmm_ops ops;
};
bool vmm_space_init(struct vmm_space *space, uint64_t root_phys,
                    unsigned int physical_bits, bool nx_enabled,
                    const struct vmm_ops *ops);
bool vmm_space_query(const struct vmm_space *space, uint64_t virt,
                     struct vmm_mapping *out);
bool vmm_space_map(struct vmm_space *space, uint64_t virt,
                   uint64_t phys, uint64_t flags);
bool vmm_space_unmap(struct vmm_space *space, uint64_t virt);
bool vmm_space_protect(struct vmm_space *space, uint64_t virt, uint64_t flags);
#endif
