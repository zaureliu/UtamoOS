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

#endif
