/* SPDX-License-Identifier: MIT */
#include <utamo/memory_selftest.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/vmm.h>
#include <utamo/paging.h>
#include <utamo/panic.h>

#define UTAMO_PMM_STRESS_PAGES 64u
#define UTAMO_VMM_STRESS_PAGES 16u

bool memory_pmm_selftest(void)
{
    struct pmm_stats before, after;
    uint64_t pages[UTAMO_PMM_STRESS_PAGES] = {0};
    size_t count = 0u;
    bool good = pmm_get_stats(&before);
    if (!good) {
        return false;
    }
    good = !pmm_free_page(0) && !pmm_free_page(before.bitmap_phys);
    for (size_t i = 0u; good && i < UTAMO_PMM_STRESS_PAGES; ++i) {
        void *pointer;
        if (!pmm_alloc_page(&pages[i])) {
            good = false;
            break;
        }
        ++count;
        for (size_t j = 0u; j < i; ++j) {
            if (pages[j] == pages[i]) {
                good = false;
            }
        }
        if (!memory_phys_to_virt(pages[i], (size_t)MEMORY_PAGE_SIZE, &pointer)) {
            good = false;
            break;
        }
        volatile uint64_t *words = pointer;
        const uint64_t marker = UINT64_C(0x5554414d4f000000) + (uint64_t)i;
        words[0] = marker;
        words[511] = ~marker;
        if (words[0] != marker || words[511] != ~marker ||
            pmm_free_page(pages[i] + 1u)) {
            good = false;
        }
    }
    while (count != 0u) {
        --count;
        if (!pmm_free_page(pages[count]) || pmm_free_page(pages[count])) {
            good = false;
        }
    }
    /* Exercise contiguous ownership and repeat allocation after the stress. */
    uint64_t run;
    if (good && pmm_alloc_pages(4u, &run)) {
        if (!pmm_free_pages(run, 4u) || pmm_free_pages(run, 4u)) {
            good = false;
        }
    } else {
        good = false;
    }
    if (!pmm_get_stats(&after) || after.free_frames != before.free_frames ||
        after.used_frames != before.used_frames) {
        good = false;
    }
    return good;
}

static uint64_t test_address(size_t index)
{
    if (index < 8u) {
        return VMM_TEST_BASE + (uint64_t)index * MEMORY_PAGE_SIZE;
    }
    /* Exercise both sides of a PT boundary in the dedicated 4-MiB test range. */
    return VMM_TEST_BASE + UINT64_C(0x200000) +
           (uint64_t)(index - 8u) * MEMORY_PAGE_SIZE - 4u * MEMORY_PAGE_SIZE;
}

bool memory_vmm_selftest(void)
{
    struct pmm_stats before, after;
    struct vmm_info info_before, info_after;
    uint64_t pages[UTAMO_VMM_STRESS_PAGES] = {0};
    bool allocated[UTAMO_VMM_STRESS_PAGES] = {false};
    bool mapped[UTAMO_VMM_STRESS_PAGES] = {false};
    if (!pmm_get_stats(&before) || !vmm_get_info(&info_before)) {
        return false;
    }
    const uint64_t nx = info_before.nx_enabled ? VMM_NX : 0u;
    const uint64_t flags = VMM_PRESENT | VMM_WRITABLE | nx;
    bool good = true;
    for (size_t i = 0u; good && i < UTAMO_VMM_STRESS_PAGES; ++i) {
        struct vmm_mapping query;
        const uint64_t virt = test_address(i);
        if (!vmm_query_page(virt, &query) || query.mapped ||
            !pmm_alloc_page(&pages[i])) {
            good = false;
            break;
        }
        allocated[i] = true;
        if (!vmm_map_page(virt, pages[i], flags)) {
            good = false;
            break;
        }
        mapped[i] = true;
        const uint64_t marker = UINT64_C(0x564d4d5445535400) + (uint64_t)i;
        memory_write_address(virt, marker);
        const volatile uint64_t *word = (const volatile uint64_t *)(uintptr_t)virt;
        void *alias;
        if (*word != marker ||
            !memory_phys_to_virt(pages[i], (size_t)MEMORY_PAGE_SIZE, &alias) ||
            *(const volatile uint64_t *)alias != marker ||
            !vmm_query_page(virt + 123u, &query) || !query.mapped ||
            query.physical != pages[i] + 123u || query.page_size != MEMORY_PAGE_SIZE ||
            (query.flags & (VMM_PRESENT | VMM_WRITABLE | VMM_USER | VMM_NX)) != flags ||
            vmm_map_page(virt, pages[i], flags)) {
            good = false;
        }
    }
    if (good) {
        struct vmm_mapping query;
        good = vmm_protect_page(VMM_TEST_BASE, VMM_PRESENT | nx) &&
               vmm_query_page(VMM_TEST_BASE, &query) && query.mapped &&
               (query.flags & VMM_WRITABLE) == 0u &&
               !vmm_protect_page(VMM_TEST_BASE, flags | VMM_USER) &&
               vmm_query_page(VMM_TEST_BASE, &query) &&
               (query.flags & (VMM_USER | VMM_WRITABLE)) == 0u &&
               !vmm_map_page(VMM_TEST_BASE + 32u * MEMORY_PAGE_SIZE,
                              pages[0], flags | VMM_USER) &&
               vmm_protect_page(VMM_TEST_BASE, flags);
        if (good) {
            memory_write_address(VMM_TEST_BASE, UINT64_C(0xabcdef0123456789));
            good = *(const volatile uint64_t *)(uintptr_t)VMM_TEST_BASE ==
                   UINT64_C(0xabcdef0123456789);
        }
    }
    for (size_t i = 1u; good && i < UTAMO_VMM_STRESS_PAGES; i += 2u) {
        const uint64_t virt = test_address(i);
        struct vmm_mapping query;
        if (!vmm_unmap_page(virt)) {
            good = false;
            break;
        }
        mapped[i] = false;
        if (!vmm_query_page(virt, &query) || query.mapped || vmm_unmap_page(virt) ||
            !vmm_map_page(virt, pages[i], flags)) {
            good = false;
            break;
        }
        mapped[i] = true;
        const uint64_t marker = UINT64_C(0x564d4d5445535400) + (uint64_t)i;
        if (*(const volatile uint64_t *)(uintptr_t)virt != marker) {
            good = false;
        }
    }
    for (size_t i = 0u; i < UTAMO_VMM_STRESS_PAGES; ++i) {
        if (mapped[i]) {
            if (!vmm_unmap_page(test_address(i))) {
                /* Do not free a possibly still mapped frame on a failed cleanup. */
                good = false;
                continue;
            }
            struct vmm_mapping query;
            if (!vmm_query_page(test_address(i), &query) || query.mapped) {
                good = false;
                continue;
            }
        }
        if (allocated[i] && !pmm_free_page(pages[i])) {
            good = false;
        }
    }
    if (!pmm_get_stats(&after) || !vmm_get_info(&info_after) ||
        info_after.table_pages < info_before.table_pages ||
        after.used_frames < before.used_frames ||
        after.used_frames - before.used_frames !=
            info_after.table_pages - info_before.table_pages) {
        good = false;
    }
    return good;
}

/* Explicit fatal probes. They are never called during normal initialization. */
static uint64_t prepare_fault_page(void)
{
    struct vmm_mapping mapping;
    struct vmm_info info;
    uint64_t phys;
    if (!vmm_get_info(&info) ||
        !vmm_query_page(VMM_TEST_BASE, &mapping) || mapping.mapped ||
        !pmm_alloc_page(&phys)) {
        PANIC("Cannot prepare VMM fault probe");
    }
    const uint64_t flags = VMM_PRESENT | VMM_WRITABLE |
                           (info.nx_enabled ? VMM_NX : 0u);
    if (!vmm_map_page(VMM_TEST_BASE, phys, flags)) {
        (void)pmm_free_page(phys);
        PANIC("Cannot map VMM fault probe");
    }
    memory_write_address(VMM_TEST_BASE, UINT64_C(0xc3)); /* RET for NX probe. */
    return phys;
}

_Noreturn void memory_fault_unmapped(void)
{
    const uint64_t phys = prepare_fault_page();
    if (!vmm_unmap_page(VMM_TEST_BASE) || !pmm_free_page(phys)) {
        PANIC("Cannot unmap VMM fault probe");
    }
    memory_write_address(VMM_TEST_BASE, 0);
    PANIC("Unmapped access did not fault");
}

_Noreturn void memory_fault_readonly(void)
{
    (void)prepare_fault_page();
    struct vmm_info info;
    if (!vmm_get_info(&info) ||
        !vmm_protect_page(VMM_TEST_BASE, VMM_PRESENT | (info.nx_enabled ? VMM_NX : 0u))) {
        PANIC("Cannot protect readonly fault probe");
    }
    memory_write_address(VMM_TEST_BASE, 0);
    PANIC("Readonly access did not fault");
}

_Noreturn void memory_fault_nx(void)
{
    struct vmm_info info;
    if (!vmm_get_info(&info) || !info.nx_enabled) {
        PANIC("NX unavailable for deliberate probe");
    }
    (void)prepare_fault_page();
    memory_execute_address(VMM_TEST_BASE);
    PANIC("NX access did not fault");
}
