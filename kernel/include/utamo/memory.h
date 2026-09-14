/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_MEMORY_H
#define UTAMO_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/memory_map.h>
#define MEMORY_PAGE_SIZE UINT64_C(4096)
bool memory_align_up(uint64_t value, uint64_t *out);
uint64_t memory_align_down(uint64_t value);
bool memory_is_page_aligned(uint64_t value);
bool memory_is_canonical(uint64_t address);
bool memory_physical_mask(unsigned int bits, uint64_t *out);
unsigned int memory_page_index(uint64_t virt, unsigned int level);
/* Bootstrap setup, never reclaims bootloader memory. */
struct framebuffer;
bool memory_init(const struct memory_map *map, const struct framebuffer *fb);
/* Checked access only to the types mapped by Limine base revision 3.
 * A successful conversion describes an existing boot mapping, not ownership. */
bool memory_phys_to_virt(uint64_t phys, size_t bytes, void **out);
bool memory_hhdm_to_phys(const void *virt, size_t bytes, uint64_t *out);
#endif
