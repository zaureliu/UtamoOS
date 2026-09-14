/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_HEAP_PAGES_H
#define UTAMO_HEAP_PAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/vmm.h>

/*
 * The caller owns serialization and the entire virtual interval. Callbacks
 * are non-reentrant: alloc returns one exclusively owned, aligned frame;
 * zero initializes that entire frame through its HHDM alias; map/unmap
 * return true only after changing exactly one 4-KiB mapping. A false alloc,
 * map, unmap or free must leave its respective ownership/mapping unchanged.
 * query has the same success/absence contract as vmm_query_page().
 *
 * No IRQ/NMI allocation is permitted. The kernel wrapper keeps IF disabled
 * across a growth transaction. These pure callbacks execute no privileged
 * instruction themselves and allow host fixtures.
 */
struct heap_page_ops {
    void *context;
    bool (*alloc)(void *context, uint64_t *phys);
    bool (*free)(void *context, uint64_t phys);
    bool (*zero)(void *context, uint64_t phys);
    bool (*map)(void *context, uint64_t virt, uint64_t phys, uint64_t flags);
    bool (*query)(void *context, uint64_t virt, struct vmm_mapping *mapping);
    bool (*unmap)(void *context, uint64_t virt);
};

struct heap_pages {
    struct heap_page_ops ops;
    uint64_t base;
    size_t capacity_bytes;
    size_t mapped_bytes;
    bool nx;
    bool initialized;
    bool corrupted;
};

/*
 * state must initially be zero-initialized. This helper accepts only aligned
 * intervals inside the owned VMM arena, beyond its first 4-MiB test region.
 * The caller reserves its own disjoint heap subrange before initialization.
 * This does not allocate, query or map any page. Invalid arguments leave state
 * unchanged; an initialized instance cannot be initialized a second time.
 */
bool heap_pages_init(struct heap_pages *state, uint64_t base,
                     size_t capacity_bytes, bool nx,
                     const struct heap_page_ops *ops);

/*
 * Grow an initialized prefix [base, base+old_mapped) to new_mapped bytes.
 * Sizes are page-aligned; old_mapped must equal the committed mapped_bytes.
 * No shrink. Equal sizes are a successful no-op. New pages use fixed
 * supervisor WB PRESENT|WRITABLE flags, plus NX when enabled at init.
 *
 * A failed transaction rolls back only newly added data pages; inherited
 * prefix mappings are untouched. Empty VMM intermediate tables may remain
 * pinned, as specified by the VMM. No journal allocation is required.
 * Impossible query/unmap/free cleanup failures set corrupted permanently.
 * The kernel caller must panic on corruption, never resume heap use.
 */
bool heap_pages_grow(struct heap_pages *state, size_t old_mapped,
                     size_t new_mapped);

#endif
