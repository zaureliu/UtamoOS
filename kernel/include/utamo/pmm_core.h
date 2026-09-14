/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PMM_CORE_H
#define UTAMO_PMM_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/memory_map.h>

struct pmm_plan {
    uint64_t storage_phys;
    uint64_t storage_bytes; /* Page-rounded space for both bitmaps. */
    uint64_t bitmap_bytes;  /* Exact byte count of each individual bitmap. */
    uint64_t span_frames;   /* Includes physical holes below the last usable frame. */
};

struct pmm_stats {
    uint64_t total_frames; /* Original USABLE frames; excludes reserved map types. */
    uint64_t free_frames;
    uint64_t used_frames;  /* Includes metadata and permanent usable reservations. */
    uint64_t bitmap_phys;
    uint64_t bitmap_bytes;
    uint64_t storage_bytes;
    uint64_t span_frames;
};

/* Pure allocator state. Initialize to zero before the first initialization.
 * Only these functions may mutate it. Its storage and the map must not overlap.
 * Eligible=0/occupied=1 means permanently unavailable; eligible=1/occupied=1
 * means allocated and individually releasable. Two one-bit-per-frame maps
 * distinguish permanent reservations from allocations without a heap/list.
 */
struct pmm_state {
    unsigned char *eligible;
    unsigned char *occupied;
    struct pmm_plan plan;
    uint64_t total_frames;
    uint64_t free_frames;
    uint64_t cursor;
    bool initialized;
};

/* Input map obeys the memory_map_add contract. All failures preserve outputs.
 * The plan selects storage in USABLE memory; page zero is never selected.
 */
bool pmm_plan(const struct memory_map *map, struct pmm_plan *out_plan);
/* storage is a valid writable object of at least plan->storage_bytes bytes,
 * corresponding to plan->storage_phys. The caller validates that physical
 * range is accessible (HHDM in the kernel). No physical address is dereferenced.
 */
bool pmm_core_init(struct pmm_state *state, const struct memory_map *map,
                   const struct pmm_plan *plan, void *storage);
bool pmm_core_alloc_pages(struct pmm_state *state, size_t count, uint64_t *out_phys);
bool pmm_core_free_pages(struct pmm_state *state, uint64_t phys, size_t count);
/* Permanent reservations round outward to whole pages. Already unavailable
 * pages are allowed, but any allocated frame rejects the entire operation.
 * Ranges outside the managed physical span are already unavailable.
 */
bool pmm_core_reserve_range(struct pmm_state *state, uint64_t phys, size_t bytes);
/* Convert one allocated frame into a permanent reservation, e.g. a page table.
 * It remains used; subsequent free and pin operations return false.
 */
bool pmm_core_pin_page(struct pmm_state *state, uint64_t phys);
/* True only for a caller-owned allocation, never metadata/pinned tables. */
bool pmm_core_is_allocated_page(const struct pmm_state *state, uint64_t phys);
bool pmm_core_get_stats(const struct pmm_state *state, struct pmm_stats *out_stats);

#endif
