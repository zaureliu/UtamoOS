/* SPDX-License-Identifier: MIT */
#include <utamo/pmm_core.h>
#include <utamo/memory.h>
#include <utamo/string.h>

static bool bitmap_test(const unsigned char *bitmap, uint64_t frame)
{
    const unsigned int bit = (unsigned int)(frame & UINT64_C(7));
    return (bitmap[(size_t)(frame / 8u)] & (1u << bit)) != 0;
}

static void bitmap_set(unsigned char *bitmap, uint64_t frame, bool value)
{
    const size_t index = (size_t)(frame / 8u);
    const unsigned int bit = (unsigned int)(frame & UINT64_C(7));
    const unsigned char mask = (unsigned char)(1u << bit);
    if (value) {
        bitmap[index] |= mask;
    } else {
        bitmap[index] &= (unsigned char)~mask;
    }
}

bool pmm_plan(const struct memory_map *map, struct pmm_plan *out_plan)
{
    if (map == NULL || out_plan == NULL || map->count == 0 ||
        map->count > UTAMO_MEMORY_REGION_LIMIT) {
        return false;
    }
    uint64_t last_end = 0;
    uint64_t usable_bytes = 0;
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (region->length == 0 || region->length > UINT64_MAX - region->base ||
            (unsigned int)region->type > (unsigned int)UTAMO_MEMORY_FRAMEBUFFER ||
            (i != 0 && region->base < map->regions[i - 1].base)) {
            return false;
        }
        if (region->type != UTAMO_MEMORY_USABLE) {
            continue;
        }
        if (!memory_is_page_aligned(region->base) ||
            !memory_is_page_aligned(region->length) ||
            region->length > UINT64_MAX - usable_bytes) {
            return false;
        }
        const uint64_t end = region->base + region->length;
        if (end > last_end) {
            last_end = end;
        }
        usable_bytes += region->length;
    }
    if (last_end == 0 || usable_bytes != map->usable_bytes) {
        return false;
    }
    const uint64_t frames = last_end / MEMORY_PAGE_SIZE;
    const uint64_t bitmap_bytes = frames / 8u + (frames % 8u != 0 ? 1u : 0u);
    uint64_t storage_bytes;
    if (bitmap_bytes > UINT64_MAX / 2u ||
        !memory_align_up(bitmap_bytes * 2u, &storage_bytes) ||
        storage_bytes > SIZE_MAX) {
        return false;
    }
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (region->type != UTAMO_MEMORY_USABLE) {
            continue;
        }
        const uint64_t start = region->base == 0 ? MEMORY_PAGE_SIZE : region->base;
        const uint64_t end = region->base + region->length;
        if (start <= end && storage_bytes <= end - start) {
            *out_plan = (struct pmm_plan){
                .storage_phys = start,
                .storage_bytes = storage_bytes,
                .bitmap_bytes = bitmap_bytes,
                .span_frames = frames
            };
            return true;
        }
    }
    return false;
}

static bool range_frames(const struct pmm_state *state, uint64_t phys,
                         size_t count, uint64_t *first, uint64_t *end)
{
    if (state == NULL || !state->initialized || count == 0 ||
        !memory_is_page_aligned(phys)) {
        return false;
    }
    const uint64_t frame = phys / MEMORY_PAGE_SIZE;
    if (frame >= state->plan.span_frames ||
        (uint64_t)count > state->plan.span_frames - frame) {
        return false;
    }
    *first = frame;
    *end = frame + (uint64_t)count;
    return true;
}

bool pmm_core_reserve_range(struct pmm_state *state, uint64_t phys, size_t bytes)
{
    if (state == NULL || !state->initialized || bytes == 0 ||
        (uint64_t)bytes > UINT64_MAX - phys) {
        return false;
    }
    uint64_t end_address;
    if (!memory_align_up(phys + (uint64_t)bytes, &end_address)) {
        return false;
    }
    const uint64_t first = phys / MEMORY_PAGE_SIZE;
    uint64_t end = end_address / MEMORY_PAGE_SIZE;
    if (end > state->plan.span_frames) {
        end = state->plan.span_frames;
    }
    for (uint64_t frame = first; frame < end; ++frame) {
        if (bitmap_test(state->eligible, frame) &&
            bitmap_test(state->occupied, frame)) {
            return false;
        }
    }
    for (uint64_t frame = first; frame < end; ++frame) {
        if (bitmap_test(state->eligible, frame)) {
            bitmap_set(state->eligible, frame, false);
            bitmap_set(state->occupied, frame, true);
            --state->free_frames;
        }
    }
    return true;
}

bool pmm_core_init(struct pmm_state *state, const struct memory_map *map,
                   const struct pmm_plan *plan, void *storage)
{
    struct pmm_plan expected;
    if (state == NULL || state->initialized || plan == NULL || storage == NULL ||
        !pmm_plan(map, &expected) ||
        plan->storage_phys != expected.storage_phys ||
        plan->storage_bytes != expected.storage_bytes ||
        plan->bitmap_bytes != expected.bitmap_bytes ||
        plan->span_frames != expected.span_frames) {
        return false;
    }
    struct pmm_state initial = {
        .eligible = storage,
        .occupied = (unsigned char *)storage + (size_t)plan->bitmap_bytes,
        .plan = *plan,
        .total_frames = map->usable_bytes / MEMORY_PAGE_SIZE,
        .free_frames = map->usable_bytes / MEMORY_PAGE_SIZE,
        .cursor = 0,
        .initialized = true
    };
    (void)memset(storage, 0, (size_t)plan->storage_bytes);
    (void)memset(initial.occupied, 0xff, (size_t)plan->bitmap_bytes);
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (region->type != UTAMO_MEMORY_USABLE) {
            continue;
        }
        const uint64_t first = region->base / MEMORY_PAGE_SIZE;
        const uint64_t end = (region->base + region->length) / MEMORY_PAGE_SIZE;
        for (uint64_t frame = first; frame < end; ++frame) {
            bitmap_set(initial.eligible, frame, true);
            bitmap_set(initial.occupied, frame, false);
        }
    }
    /* Both ranges are aligned, checked, and cannot contain allocations yet. */
    (void)pmm_core_reserve_range(&initial, 0, (size_t)MEMORY_PAGE_SIZE);
    (void)pmm_core_reserve_range(&initial, plan->storage_phys,
                                  (size_t)plan->storage_bytes);
    *state = initial;
    return true;
}

static bool find_run(const struct pmm_state *state, uint64_t start,
                     uint64_t count, uint64_t *out_frame)
{
    uint64_t run = 0;
    for (uint64_t frame = start; frame < state->plan.span_frames; ++frame) {
        if (!bitmap_test(state->occupied, frame)) {
            ++run;
            if (run == count) {
                *out_frame = frame - (count - 1u);
                return true;
            }
        } else {
            run = 0;
        }
    }
    return false;
}

bool pmm_core_alloc_pages(struct pmm_state *state, size_t count, uint64_t *out_phys)
{
    if (state == NULL || !state->initialized || out_phys == NULL || count == 0 ||
        (uint64_t)count > state->free_frames) {
        return false;
    }
    uint64_t first;
    /* The second scan includes runs straddling the cursor, not frame zero.
     * O(span_frames) worst case; no restart at zero on successful next-fit.
     */
    if (!find_run(state, state->cursor, (uint64_t)count, &first) &&
        (state->cursor == 0 || !find_run(state, 0, (uint64_t)count, &first))) {
        return false;
    }
    const uint64_t end = first + (uint64_t)count;
    for (uint64_t frame = first; frame < end; ++frame) {
        bitmap_set(state->occupied, frame, true);
    }
    state->free_frames -= (uint64_t)count;
    state->cursor = end == state->plan.span_frames ? 0 : end;
    *out_phys = first * MEMORY_PAGE_SIZE;
    return true;
}

bool pmm_core_free_pages(struct pmm_state *state, uint64_t phys, size_t count)
{
    uint64_t first;
    uint64_t end;
    if (!range_frames(state, phys, count, &first, &end)) {
        return false;
    }
    for (uint64_t frame = first; frame < end; ++frame) {
        if (!bitmap_test(state->eligible, frame) ||
            !bitmap_test(state->occupied, frame)) {
            return false;
        }
    }
    for (uint64_t frame = first; frame < end; ++frame) {
        bitmap_set(state->occupied, frame, false);
    }
    state->free_frames += (uint64_t)count;
    return true;
}

bool pmm_core_pin_page(struct pmm_state *state, uint64_t phys)
{
    uint64_t first;
    uint64_t end;
    if (!range_frames(state, phys, 1, &first, &end) ||
        !bitmap_test(state->eligible, first) ||
        !bitmap_test(state->occupied, first)) {
        return false;
    }
    bitmap_set(state->eligible, first, false);
    return true;
}

bool pmm_core_is_allocated_page(const struct pmm_state *state, uint64_t phys)
{
    uint64_t first;
    uint64_t end;
    return range_frames(state, phys, 1, &first, &end) &&
           bitmap_test(state->eligible, first) &&
           bitmap_test(state->occupied, first);
}

bool pmm_core_get_stats(const struct pmm_state *state, struct pmm_stats *out_stats)
{
    if (state == NULL || !state->initialized || out_stats == NULL) {
        return false;
    }
    *out_stats = (struct pmm_stats){
        .total_frames = state->total_frames,
        .free_frames = state->free_frames,
        .used_frames = state->total_frames - state->free_frames,
        .bitmap_phys = state->plan.storage_phys,
        .bitmap_bytes = state->plan.bitmap_bytes,
        .storage_bytes = state->plan.storage_bytes,
        .span_frames = state->plan.span_frames
    };
    return true;
}
