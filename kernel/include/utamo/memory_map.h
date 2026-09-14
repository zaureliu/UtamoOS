/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_MEMORY_MAP_H
#define UTAMO_MEMORY_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTAMO_MEMORY_REGION_LIMIT 512u

enum memory_type {
    UTAMO_MEMORY_USABLE,
    UTAMO_MEMORY_RESERVED,
    UTAMO_MEMORY_ACPI_RECLAIMABLE,
    UTAMO_MEMORY_ACPI_NVS,
    UTAMO_MEMORY_BAD,
    UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE,
    UTAMO_MEMORY_KERNEL_AND_MODULES,
    UTAMO_MEMORY_FRAMEBUFFER
};

struct memory_region {
    uint64_t base;   /* Physical address; never dereferenced. */
    uint64_t length;
    enum memory_type type;
};

struct memory_map {
    struct memory_region regions[UTAMO_MEMORY_REGION_LIMIT];
    size_t count;
    uint64_t usable_bytes;
    uint64_t bootloader_reclaimable_bytes;
};

void memory_map_init(struct memory_map *map);
/* Append in ascending base order. Invalid input leaves the map unchanged.
 * Only maps initialized here and mutated through this API are valid inputs.
 */
bool memory_map_add(struct memory_map *map, const struct memory_region *region);

#endif
