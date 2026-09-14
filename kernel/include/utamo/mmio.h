/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_MMIO_H
#define UTAMO_MMIO_H
#include <utamo/memory_map.h>
#define UTAMO_MMIO_BASE UINT64_C(0xffffc00080000000)
#define UTAMO_MMIO_SIZE UINT64_C(0x400000)
#define UTAMO_MMIO_MAX UINT64_C(0x100000)
/* Only validated PCI apertures, never RAM; page-aligned physical extent.
 * Permanent supervisor UC mappings. No IRQ callers or runtime unmapping. */
bool mmio_range_allowed(const struct memory_map *map, uint64_t physical, size_t bytes);
bool vmm_map_mmio(uint64_t physical, size_t bytes, void **out);
#endif
