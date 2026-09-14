/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_BOOT_H
#define UTAMO_BOOT_H

#include <stdbool.h>
#include <utamo/framebuffer.h>
#include <utamo/memory_map.h>

/* Limine structures are confined to the x86_64 boot adapter. */
bool boot_protocol_supported(void);
bool boot_paging_supported(void);
bool boot_init_framebuffer(struct framebuffer *framebuffer);
bool boot_read_memory_map(struct memory_map *map);

struct boot_memory_layout {
    uint64_t hhdm_offset;
    uint64_t kernel_phys;
    uint64_t kernel_virt;
};
/* Copy request responses; callers validate ranges against the copied map. */
bool boot_read_memory_layout(struct boot_memory_layout *out);

#endif
