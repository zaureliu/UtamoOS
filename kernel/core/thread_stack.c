/* SPDX-License-Identifier: MIT */
#include <utamo/thread_stack.h>
#include <utamo/heap_pages.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/panic.h>
#include <utamo/string.h>

/* BSP ownership, protected by IF save/disable/restore. Epochs never wrap. */
static uint64_t occupied;
static uint64_t generations[UTAMO_THREAD_STACK_SLOTS];

static bool allocate_page(void *context, uint64_t *phys)
{
    (void)context;
    return pmm_alloc_page(phys);
}

static bool release_page(void *context, uint64_t phys)
{
    (void)context;
    return pmm_free_page(phys);
}

static bool zero_page(void *context, uint64_t phys)
{
    (void)context;
    void *alias;
    if (!memory_phys_to_virt(phys, (size_t)MEMORY_PAGE_SIZE, &alias)) {
        return false;
    }
    memset(alias, 0, (size_t)MEMORY_PAGE_SIZE);
    return true;
}

static bool map_page(void *context, uint64_t virt, uint64_t phys, uint64_t flags)
{
    (void)context;
    return vmm_map_page(virt, phys, flags);
}

static bool query_page(void *context, uint64_t virt, struct vmm_mapping *mapping)
{
    (void)context;
    return vmm_query_page(virt, mapping);
}

static bool unmap_page(void *context, uint64_t virt)
{
    (void)context;
    return vmm_unmap_page(virt);
}

static bool descriptor_valid(const struct thread_stack *stack)
{
    if (stack == NULL || !stack->active || stack->slot >= UTAMO_THREAD_STACK_SLOTS ||
        stack->generation == 0u ||
        generations[stack->slot] != stack->generation ||
        (occupied & (UINT64_C(1) << stack->slot)) == 0u) {
        return false;
    }
    const uint64_t guard = UTAMO_THREAD_STACK_REGION +
                           (uint64_t)stack->slot * UTAMO_THREAD_STACK_STRIDE;
    return stack->guard == guard && stack->base == guard + MEMORY_PAGE_SIZE &&
           stack->top == stack->base + UTAMO_THREAD_STACK_SIZE;
}

bool thread_stack_alloc(struct thread_stack *out)
{
    if (out == NULL) {
        return false;
    }
    const uint64_t saved = cpu_irq_save();
    struct vmm_info info;
    if (!vmm_get_info(&info)) {
        cpu_irq_restore(saved);
        return false;
    }
    size_t slot = 0u;
    for (; slot < UTAMO_THREAD_STACK_SLOTS; ++slot) {
        if ((occupied & (UINT64_C(1) << slot)) == 0u &&
            generations[slot] != UINT64_MAX) {
            break;
        }
    }
    if (slot == UTAMO_THREAD_STACK_SLOTS) {
        cpu_irq_restore(saved);
        return false;
    }
    const uint64_t guard = UTAMO_THREAD_STACK_REGION +
                           (uint64_t)slot * UTAMO_THREAD_STACK_STRIDE;
    struct vmm_mapping mapping;
    if (!vmm_query_page(guard, &mapping) || mapping.mapped) {
        cpu_irq_restore(saved);
        return false;
    }
    const struct heap_page_ops ops = {
        .alloc = allocate_page, .free = release_page, .zero = zero_page,
        .map = map_page, .query = query_page, .unmap = unmap_page
    };
    struct heap_pages backing = {0};
    occupied |= UINT64_C(1) << slot;
    const bool good = heap_pages_init(&backing, guard + MEMORY_PAGE_SIZE,
                                      UTAMO_THREAD_STACK_SIZE, info.nx_enabled, &ops) &&
                      heap_pages_grow(&backing, 0u, UTAMO_THREAD_STACK_SIZE);
    if (!good) {
        if (backing.corrupted) {
            PANIC("Thread stack rollback lost ownership");
        }
        occupied &= ~(UINT64_C(1) << slot);
        cpu_irq_restore(saved);
        return false;
    }
    ++generations[slot];
    *out = (struct thread_stack){
        .guard = guard, .base = guard + MEMORY_PAGE_SIZE,
        .top = guard + MEMORY_PAGE_SIZE + UTAMO_THREAD_STACK_SIZE,
        .slot = slot, .generation = generations[slot], .active = true
    };
    cpu_irq_restore(saved);
    return true;
}

bool thread_stack_free(struct thread_stack *stack)
{
    const uint64_t saved = cpu_irq_save();
    /* The automatic anchor is on the currently executing C stack. */
    const uintptr_t anchor = (uintptr_t)&saved;
    if (!descriptor_valid(stack) ||
        (anchor >= stack->base && anchor < stack->top)) {
        cpu_irq_restore(saved);
        return false;
    }
    struct vmm_mapping mapping;
    if (!vmm_query_page(stack->guard, &mapping) || mapping.mapped) {
        PANIC("Thread stack guard ownership changed");
    }
    /*
     * Validate the full extent before removing anything. Query does not allocate.
     * Each frame was allocated exclusively by the stack transaction.
     */
    for (uint64_t virt = stack->base; virt < stack->top; virt += MEMORY_PAGE_SIZE) {
        if (!vmm_query_page(virt, &mapping) || !mapping.mapped ||
            mapping.page_size != MEMORY_PAGE_SIZE ||
            !memory_is_page_aligned(mapping.physical) ||
            !pmm_is_allocated_page(mapping.physical)) {
            PANIC("Thread stack mapping ownership changed");
        }
    }
    for (uint64_t virt = stack->base; virt < stack->top; virt += MEMORY_PAGE_SIZE) {
        if (!vmm_query_page(virt, &mapping) || !mapping.mapped) {
            PANIC("Thread stack disappeared during cleanup");
        }
        const uint64_t phys = mapping.physical;
        if (!vmm_unmap_page(virt) ||
            !vmm_query_page(virt, &mapping) || mapping.mapped ||
            !pmm_free_page(phys)) {
            PANIC("Thread stack cleanup lost ownership");
        }
    }
    occupied &= ~(UINT64_C(1) << stack->slot);
    *stack = (struct thread_stack){0};
    cpu_irq_restore(saved);
    return true;
}

bool thread_frame_init(const struct thread_stack *stack, uint64_t entry,
                       uint64_t return_sentinel, struct interrupt_frame **out)
{
    if (out == NULL || entry == 0u || return_sentinel == 0u ||
        !memory_is_canonical(entry) || !memory_is_canonical(return_sentinel)) {
        return false;
    }
    const uint64_t saved = cpu_irq_save();
    if (!descriptor_valid(stack)) {
        cpu_irq_restore(saved);
        return false;
    }
    const uint64_t page = stack->top - MEMORY_PAGE_SIZE;
    const uint64_t rsp = stack->top - sizeof(uint64_t);
    const uint64_t address = (rsp - sizeof(struct interrupt_frame)) & ~UINT64_C(15);
    struct vmm_mapping mapping;
    void *alias;
    if (!vmm_query_page(page, &mapping) || !mapping.mapped ||
        mapping.page_size != MEMORY_PAGE_SIZE ||
        (mapping.flags & (VMM_PRESENT | VMM_WRITABLE | VMM_USER)) !=
            (VMM_PRESENT | VMM_WRITABLE) ||
        !memory_is_page_aligned(mapping.physical) ||
        !memory_phys_to_virt(mapping.physical, (size_t)MEMORY_PAGE_SIZE, &alias)) {
        cpu_irq_restore(saved);
        return false;
    }
    const struct interrupt_frame frame = {
        .rip = entry, .cs = 8u, .rflags = UINT64_C(0x202), .rsp = rsp, .ss = 16u
    };
    memcpy((unsigned char *)alias + (size_t)(address - page), &frame, sizeof(frame));
    memcpy((unsigned char *)alias + (size_t)(rsp - page),
           &return_sentinel, sizeof(return_sentinel));
    *out = (struct interrupt_frame *)(uintptr_t)address;
    cpu_irq_restore(saved);
    return true;
}
