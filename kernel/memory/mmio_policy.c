/* SPDX-License-Identifier: MIT */
#include <utamo/mmio.h>
bool mmio_range_allowed(const struct memory_map *map, uint64_t physical, size_t bytes)
{
    if (map == NULL || map->count == 0u || map->count > UTAMO_MEMORY_REGION_LIMIT ||
        physical < UINT64_C(0x100000) || (physical & 4095u) != 0u ||
        bytes == 0u || (bytes & 4095u) != 0u || bytes > UTAMO_MMIO_MAX ||
        bytes > UINT64_MAX - physical) { return false; }
    const uint64_t end = physical + bytes;
    for (size_t i = 0u; i < map->count; ++i) {
        const struct memory_region *r = &map->regions[i];
        if (r->length == 0u || r->length > UINT64_MAX - r->base) { return false; }
        if (physical < r->base + r->length && r->base < end &&
            r->type != UTAMO_MEMORY_RESERVED) { return false; }
    }
    return true;
}
