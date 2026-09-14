/* SPDX-License-Identifier: MIT */
#include <utamo/heap_pages.h>
#include <utamo/memory.h>

static bool valid_interval(uint64_t base, size_t capacity)
{
    const uint64_t first = VMM_DYNAMIC_BASE + VMM_TEST_SIZE;
    const uint64_t arena_end = VMM_DYNAMIC_BASE + VMM_DYNAMIC_SIZE;
    return capacity != 0u && memory_is_page_aligned(base) &&
        memory_is_page_aligned((uint64_t)capacity) &&
        base >= first && base < arena_end &&
        (uint64_t)capacity <= arena_end - base &&
        memory_is_canonical(base) &&
        memory_is_canonical(base + (uint64_t)capacity - 1u);
}

static bool valid_ops(const struct heap_page_ops *ops)
{
    return ops != NULL && ops->alloc != NULL && ops->free != NULL &&
        ops->zero != NULL && ops->map != NULL && ops->query != NULL &&
        ops->unmap != NULL;
}

bool heap_pages_init(struct heap_pages *state, uint64_t base,
                     size_t capacity_bytes, bool nx,
                     const struct heap_page_ops *ops)
{
    if (state == NULL || state->initialized || !valid_ops(ops) ||
        !valid_interval(base, capacity_bytes)) {
        return false;
    }
    *state = (struct heap_pages){
        .ops = *ops, .base = base, .capacity_bytes = capacity_bytes,
        .nx = nx, .initialized = true
    };
    return true;
}

static void release_frame(struct heap_pages *state, uint64_t phys)
{
    if (!state->ops.free(state->ops.context, phys)) {
        state->corrupted = true;
    }
}

static void rollback(struct heap_pages *state, size_t old_mapped, size_t end)
{
    while (end > old_mapped) {
        end -= (size_t)MEMORY_PAGE_SIZE;
        const uint64_t virt = state->base + (uint64_t)end;
        struct vmm_mapping mapping;
        if (!state->ops.query(state->ops.context, virt, &mapping) ||
            !mapping.mapped || mapping.page_size != MEMORY_PAGE_SIZE ||
            !memory_is_page_aligned(mapping.physical)) {
            state->corrupted = true;
            continue;
        }
        const uint64_t phys = mapping.physical;
        if (!state->ops.unmap(state->ops.context, virt)) {
            state->corrupted = true;
            continue; /* A possibly still mapped frame must not be freed. */
        }
        if (!state->ops.query(state->ops.context, virt, &mapping) ||
            mapping.mapped) {
            state->corrupted = true;
            continue;
        }
        release_frame(state, phys);
    }
}

bool heap_pages_grow(struct heap_pages *state, size_t old_mapped,
                     size_t new_mapped)
{
    if (state == NULL || !state->initialized || state->corrupted ||
        !valid_ops(&state->ops) ||
        !valid_interval(state->base, state->capacity_bytes) ||
        old_mapped != state->mapped_bytes || new_mapped < old_mapped ||
        new_mapped > state->capacity_bytes ||
        !memory_is_page_aligned((uint64_t)old_mapped) ||
        !memory_is_page_aligned((uint64_t)new_mapped)) {
        return false;
    }
    /*
     * Check the entire new range before allocating anything. A preexisting
     * mapping belongs to someone else and must never enter our rollback set.
     */
    for (size_t offset = old_mapped; offset < new_mapped;
         offset += (size_t)MEMORY_PAGE_SIZE) {
        struct vmm_mapping mapping;
        if (!state->ops.query(state->ops.context,
                              state->base + (uint64_t)offset, &mapping) ||
            mapping.mapped) {
            return false;
        }
    }
    const uint64_t flags = VMM_PRESENT | VMM_WRITABLE |
                           (state->nx ? VMM_NX : 0u);
    size_t offset = old_mapped;
    while (offset < new_mapped) {
        uint64_t phys;
        if (!state->ops.alloc(state->ops.context, &phys)) {
            rollback(state, old_mapped, offset);
            return false;
        }
        if (!memory_is_page_aligned(phys) ||
            (phys & ~VMM_ADDRESS_MASK) != 0u) {
            /* An allocator contract violation cannot be a normal OOM. */
            state->corrupted = true;
            release_frame(state, phys);
            rollback(state, old_mapped, offset);
            return false;
        }
        if (!state->ops.zero(state->ops.context, phys) ||
            !state->ops.map(state->ops.context,
                            state->base + (uint64_t)offset, phys, flags)) {
            release_frame(state, phys);
            rollback(state, old_mapped, offset);
            return false;
        }
        offset += (size_t)MEMORY_PAGE_SIZE;
    }
    state->mapped_bytes = new_mapped;
    return true;
}
