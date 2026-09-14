/* SPDX-License-Identifier: MIT */
#include <utamo/memory_map.h>

void memory_map_init(struct memory_map *map)
{
    if (map != NULL) {
        map->count = 0;
        map->usable_bytes = 0;
        map->bootloader_reclaimable_bytes = 0;
    }
}

static bool requires_exclusive_region(enum memory_type type)
{
    return type == UTAMO_MEMORY_USABLE ||
           type == UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE;
}

bool memory_map_add(struct memory_map *map, const struct memory_region *region)
{
    if (map == NULL || region == NULL || map->count >= UTAMO_MEMORY_REGION_LIMIT ||
        (unsigned int)region->type > (unsigned int)UTAMO_MEMORY_FRAMEBUFFER ||
        region->length == 0 || region->length > UINT64_MAX - region->base) {
        return false;
    }
    if (map->count != 0 &&
        region->base < map->regions[map->count - 1].base) {
        return false;
    }
    if (requires_exclusive_region(region->type) &&
        ((region->base | region->length) & UINT64_C(0xfff)) != 0) {
        return false;
    }
    const uint64_t end = region->base + region->length;
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *previous = &map->regions[i];
        if ((requires_exclusive_region(region->type) ||
             requires_exclusive_region(previous->type)) &&
            region->base < previous->base + previous->length &&
            previous->base < end) {
            return false;
        }
    }
    uint64_t usable = map->usable_bytes;
    uint64_t reclaimable = map->bootloader_reclaimable_bytes;
    if (region->type == UTAMO_MEMORY_USABLE) {
        if (region->length > UINT64_MAX - usable) {
            return false;
        }
        usable += region->length;
    } else if (region->type == UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE) {
        if (region->length > UINT64_MAX - reclaimable) {
            return false;
        }
        reclaimable += region->length;
    }
    map->regions[map->count] = *region;
    ++map->count;
    map->usable_bytes = usable;
    map->bootloader_reclaimable_bytes = reclaimable;
    return true;
}
