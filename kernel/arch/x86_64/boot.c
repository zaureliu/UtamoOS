/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include <utamo/boot.h>

/* Protocol definitions are naturally aligned, never packed. */
_Static_assert(sizeof(void *) == 8, "Limine requires 64-bit pointers");
_Static_assert(sizeof(struct limine_framebuffer_request) == 48, "request ABI");
_Static_assert(offsetof(struct limine_framebuffer_request, response) == 40,
               "response pointer ABI");
_Static_assert(offsetof(struct limine_framebuffer, pitch) == 24, "pitch ABI");
_Static_assert(offsetof(struct limine_framebuffer, edid_size) == 48, "EDID ABI");
_Static_assert(sizeof(struct limine_memmap_entry) == 24, "memory entry ABI");
_Static_assert(sizeof(struct limine_memmap_response) == 24, "memory response ABI");
_Static_assert(sizeof(struct limine_paging_mode_request) == 72, "paging ABI");

__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_paging_mode_request paging_request = {
    .id = LIMINE_PAGING_MODE_REQUEST,
    .revision = 1,
    .response = NULL,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile LIMINE_REQUESTS_END_MARKER

bool boot_protocol_supported(void)
{
    return LIMINE_BASE_REVISION_SUPPORTED;
}

bool boot_paging_supported(void)
{
    const struct limine_paging_mode_response *response = paging_request.response;
    return response != NULL && response->mode == LIMINE_PAGING_MODE_X86_64_4LVL;
}

bool boot_init_framebuffer(struct framebuffer *framebuffer)
{
    const struct limine_framebuffer_response *response = framebuffer_request.response;
    /* Limit bootstrap work even in the presence of corrupt metadata. */
    if (framebuffer == NULL || response == NULL || response->framebuffers == NULL ||
        response->framebuffer_count == 0 || response->framebuffer_count > 64) {
        return false;
    }
    for (uint64_t i = 0; i < response->framebuffer_count; ++i) {
        const struct limine_framebuffer *source = response->framebuffers[i];
        if (source == NULL || source->memory_model != LIMINE_FRAMEBUFFER_RGB ||
            source->width < 8 || source->height < 16) {
            continue;
        }
        const struct framebuffer_config config = {
            .address = source->address,
            .width = source->width,
            .height = source->height,
            .pitch = source->pitch,
            .bpp = source->bpp,
            .red_mask_size = source->red_mask_size,
            .red_mask_shift = source->red_mask_shift,
            .green_mask_size = source->green_mask_size,
            .green_mask_shift = source->green_mask_shift,
            .blue_mask_size = source->blue_mask_size,
            .blue_mask_shift = source->blue_mask_shift
        };
        if (framebuffer_init(framebuffer, &config)) {
            return true;
        }
    }
    return false;
}

static enum memory_type memory_type_from_limine(uint64_t type)
{
    switch (type) {
    case LIMINE_MEMMAP_USABLE: return UTAMO_MEMORY_USABLE;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return UTAMO_MEMORY_ACPI_RECLAIMABLE;
    case LIMINE_MEMMAP_ACPI_NVS: return UTAMO_MEMORY_ACPI_NVS;
    case LIMINE_MEMMAP_BAD_MEMORY: return UTAMO_MEMORY_BAD;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return UTAMO_MEMORY_KERNEL_AND_MODULES;
    case LIMINE_MEMMAP_FRAMEBUFFER: return UTAMO_MEMORY_FRAMEBUFFER;
    default: return UTAMO_MEMORY_RESERVED; /* Unknown future types stay unavailable. */
    }
}

bool boot_read_memory_map(struct memory_map *map)
{
    const struct limine_memmap_response *response = memmap_request.response;
    if (map == NULL) {
        return false;
    }
    memory_map_init(map);
    if (response == NULL || response->entries == NULL ||
        response->entry_count == 0 || response->entry_count > UTAMO_MEMORY_REGION_LIMIT) {
        return false;
    }
    for (uint64_t i = 0; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *source = response->entries[i];
        if (source == NULL) {
            memory_map_init(map);
            return false;
        }
        if (source->length == 0) {
            continue;
        }
        const struct memory_region region = {
            .base = source->base,
            .length = source->length,
            .type = memory_type_from_limine(source->type)
        };
        if (!memory_map_add(map, &region)) {
            memory_map_init(map);
            return false;
        }
    }
    return map->count != 0;
}
