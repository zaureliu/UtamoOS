/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_VMM_H
#define UTAMO_VMM_H
#include <stdbool.h>
#include <stdint.h>
#define VMM_PRESENT UINT64_C(1)
#define VMM_WRITABLE UINT64_C(2)
#define VMM_USER UINT64_C(4)
#define VMM_WRITE_THROUGH UINT64_C(8)
#define VMM_CACHE_DISABLE UINT64_C(16)
#define VMM_ACCESSED UINT64_C(32)
#define VMM_DIRTY UINT64_C(64)
#define VMM_HUGE UINT64_C(128)
#define VMM_GLOBAL UINT64_C(256)
#define VMM_NX (UINT64_C(1) << 63u)
#define VMM_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define VMM_DYNAMIC_BASE UINT64_C(0xffffc00000000000)
#define VMM_DYNAMIC_SIZE (UINT64_C(1) << 39u)
#define VMM_TEST_BASE VMM_DYNAMIC_BASE
#define VMM_TEST_SIZE (UINT64_C(4) * 1024u * 1024u)
struct vmm_mapping {
    bool mapped;
    uint64_t physical; /* Includes the queried byte offset. */
    uint64_t flags;    /* Leaf attributes, effective RW/USER/NX across ancestors. */
    uint64_t page_size;
};
struct vmm_info {
    uint64_t root_phys, hhdm_offset, kernel_base, table_pages;
    unsigned int physical_bits;
    bool nx_supported, nx_enabled, sections_protected;
};
/* Mutations only inside the owned dynamic PML4 slot; 4-KiB aligned.
 * PRESENT is mandatory. Existing/huge mappings are never replaced/split.
 * Unmap does not free the data frame. Empty table pages remain pinned.
 * These operations serialize with IF on the BSP; no SMP contract. */
bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
bool vmm_unmap_page(uint64_t virt);
bool vmm_protect_page(uint64_t virt, uint64_t flags);
/* Read-only, no allocation; false=unready/unsafe/invalid, true+!mapped=absent. */
bool vmm_query_page(uint64_t virt, struct vmm_mapping *out);
bool vmm_get_info(struct vmm_info *out);
#endif
