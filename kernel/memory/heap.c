/* SPDX-License-Identifier: MIT */
#include <utamo/heap.h>
#include <utamo/heap_pages.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/vmm.h>
#include <utamo/panic.h>
#include <utamo/string.h>

static struct heap_core kernel_heap;
static struct heap_pages backing;
static bool ready;

static bool allocate_frame(void *context, uint64_t *physical)
{
    (void)context;
    return pmm_alloc_page(physical);
}

static bool free_frame(void *context, uint64_t physical)
{
    (void)context;
    return pmm_free_page(physical);
}

static bool zero_frame(void *context, uint64_t physical)
{
    (void)context;
    void *address;
    if (!memory_phys_to_virt(physical, (size_t)MEMORY_PAGE_SIZE, &address)) {
        return false;
    }
    memset(address, 0, (size_t)MEMORY_PAGE_SIZE);
    return true;
}

static bool map_frame(void *context, uint64_t virtual_address,
                      uint64_t physical, uint64_t flags)
{
    (void)context;
    return vmm_map_page(virtual_address, physical, flags);
}

static bool query_frame(void *context, uint64_t virtual_address,
                        struct vmm_mapping *mapping)
{
    (void)context;
    return vmm_query_page(virtual_address, mapping);
}

static bool unmap_frame(void *context, uint64_t virtual_address)
{
    (void)context;
    return vmm_unmap_page(virtual_address);
}

static bool grow_heap(void *context, size_t old_size, size_t new_size)
{
    struct heap_pages *pages = context;
    const bool result = heap_pages_grow(pages, old_size, new_size);
    if (pages->corrupted) {
        PANIC("Heap page rollback lost ownership");
    }
    return result;
}

bool heap_init(void)
{
    const uint64_t saved = cpu_irq_save();
    struct vmm_info info;
    if (ready || !vmm_get_info(&info)) {
        cpu_irq_restore(saved);
        return false;
    }
    /* Reserve this entire virtual policy range before accepting any allocation. */
    for (size_t offset = 0u; offset < UTAMO_HEAP_MAX_SIZE;
         offset += (size_t)MEMORY_PAGE_SIZE) {
        struct vmm_mapping mapping;
        if (!vmm_query_page(UTAMO_HEAP_BASE + (uint64_t)offset, &mapping) ||
            mapping.mapped) {
            cpu_irq_restore(saved);
            return false;
        }
    }
    const struct heap_page_ops page_ops = {
        .context = NULL, .alloc = allocate_frame, .free = free_frame,
        .zero = zero_frame, .map = map_frame, .query = query_frame,
        .unmap = unmap_frame
    };
    if (!heap_pages_init(&backing, UTAMO_HEAP_BASE, UTAMO_HEAP_MAX_SIZE,
                         info.nx_enabled, &page_ops) ||
        !grow_heap(&backing, 0u, UTAMO_HEAP_INITIAL_SIZE)) {
        cpu_irq_restore(saved);
        return false;
    }
    const struct heap_ops core_ops = {.context = &backing, .grow = grow_heap};
    if (!heap_core_init(&kernel_heap, (void *)(uintptr_t)UTAMO_HEAP_BASE,
                        UTAMO_HEAP_INITIAL_SIZE, UTAMO_HEAP_MAX_SIZE,
                        UTAMO_HEAP_GROWTH_SIZE, true, &core_ops)) {
        PANIC("Heap core rejected its initialized backing");
    }
    ready = true;
    cpu_irq_restore(saved);
    return true;
}

void *kmalloc(size_t bytes)
{
    const uint64_t saved = cpu_irq_save();
    void *result = ready ? heap_core_alloc(&kernel_heap, bytes) : NULL;
    cpu_irq_restore(saved);
    return result;
}

void *kcalloc(size_t count, size_t bytes)
{
    const uint64_t saved = cpu_irq_save();
    void *result = ready ? heap_core_calloc(&kernel_heap, count, bytes) : NULL;
    cpu_irq_restore(saved);
    return result;
}

void *krealloc(void *pointer, size_t bytes)
{
    const uint64_t saved = cpu_irq_save();
    void *result = ready ? heap_core_realloc(&kernel_heap, pointer, bytes) : NULL;
    cpu_irq_restore(saved);
    return result;
}

bool kfree(void *pointer)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = pointer == NULL || (ready && heap_core_free(&kernel_heap, pointer));
    cpu_irq_restore(saved);
    return result;
}

bool heap_get_stats(struct heap_stats *stats)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = ready && heap_core_get_stats(&kernel_heap, stats);
    cpu_irq_restore(saved);
    return result;
}

bool heap_validate(void)
{
    const uint64_t saved = cpu_irq_save();
    struct heap_stats stats;
    const bool result = ready && heap_core_get_stats(&kernel_heap, &stats) &&
                        backing.mapped_bytes == stats.mapped_bytes;
    cpu_irq_restore(saved);
    return result;
}
