/* SPDX-License-Identifier: MIT */
#include <utamo/hhdm.h>
#include <utamo/memory.h>

bool hhdm_type_mapped(enum memory_type type)
{
    return type == UTAMO_MEMORY_USABLE ||
           type == UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE ||
           type == UTAMO_MEMORY_KERNEL_AND_MODULES ||
           type == UTAMO_MEMORY_FRAMEBUFFER;
}

bool hhdm_init(struct hhdm_context *context, const struct memory_map *map,
               uint64_t offset, unsigned int physical_bits)
{
    uint64_t mask;
    if (context == NULL || context->initialized || map == NULL ||
        map->count == 0 || map->count > UTAMO_MEMORY_REGION_LIMIT ||
        offset < UINT64_C(0xffff800000000000) ||
        !memory_is_page_aligned(offset) || !memory_is_canonical(offset) ||
        !memory_physical_mask(physical_bits, &mask)) {
        return false;
    }
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (region->length == 0 || region->length > UINT64_MAX - region->base ||
            (unsigned int)region->type > (unsigned int)UTAMO_MEMORY_FRAMEBUFFER ||
            (i != 0 && region->base < map->regions[i - 1u].base)) {
            return false;
        }
        if ((region->type == UTAMO_MEMORY_USABLE ||
             region->type == UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE) &&
            (!memory_is_page_aligned(region->base) ||
             !memory_is_page_aligned(region->length))) {
            return false;
        }
        if (!hhdm_type_mapped(region->type)) {
            continue;
        }
        const uint64_t last = region->base + region->length - 1u;
        if (last > (mask | (MEMORY_PAGE_SIZE - 1u)) ||
            last > UINT64_MAX - offset ||
            !memory_is_canonical(offset + region->base) ||
            !memory_is_canonical(offset + last)) {
            return false;
        }
    }
    *context = (struct hhdm_context){
        .map = map,
        .offset = offset,
        .physical_mask = mask,
        .initialized = true
    };
    return true;
}

const struct memory_region *hhdm_region(const struct hhdm_context *context,
                                        uint64_t phys, size_t bytes)
{
    if (context == NULL || !context->initialized || bytes == 0 ||
        (uint64_t)bytes - 1u > UINT64_MAX - phys) {
        return NULL;
    }
    for (size_t i = 0; i < context->map->count; ++i) {
        const struct memory_region *region = &context->map->regions[i];
        if (phys >= region->base && phys - region->base < region->length &&
            (uint64_t)bytes <= region->length - (phys - region->base)) {
            return region;
        }
    }
    return NULL;
}

bool hhdm_translate(const struct hhdm_context *context, uint64_t phys,
                    size_t bytes, uint64_t *out_virt)
{
    const struct memory_region *region = hhdm_region(context, phys, bytes);
    if (out_virt == NULL || region == NULL || !hhdm_type_mapped(region->type) ||
        phys > UINT64_MAX - context->offset) {
        return false;
    }
    const uint64_t virt = context->offset + phys;
    if ((uint64_t)bytes - 1u > UINT64_MAX - virt ||
        !memory_is_canonical(virt) ||
        !memory_is_canonical(virt + (uint64_t)bytes - 1u)) {
        return false;
    }
    *out_virt = virt;
    return true;
}

bool hhdm_reverse(const struct hhdm_context *context, uint64_t virt,
                  size_t bytes, uint64_t *out_phys)
{
    if (context == NULL || !context->initialized || out_phys == NULL ||
        virt < context->offset) {
        return false;
    }
    const uint64_t phys = virt - context->offset;
    uint64_t checked;
    if (!hhdm_translate(context, phys, bytes, &checked) || checked != virt) {
        return false;
    }
    *out_phys = phys;
    return true;
}
