/* SPDX-License-Identifier: MIT */
#include <utamo/user_vm.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
#include <utamo/paging.h>
#include <utamo/panic.h>
#include <utamo/pmm.h>
#include <utamo/string.h>

#define USER_VM_COPY_CHUNKS (USER_VM_COPY_LIMIT / (size_t)MEMORY_PAGE_SIZE + 1u)
#define USER_VM_LEAF_FLAGS (VMM_PRESENT | VMM_WRITABLE | VMM_USER | \
                            VMM_ACCESSED | VMM_DIRTY | VMM_NX)

static bool valid_address(uint64_t virt)
{
    return virt >= USER_VM_MIN && virt < USER_VM_END;
}

static bool valid_access(uint32_t access)
{
    return (access & ~(USER_VM_WRITE | USER_VM_EXEC)) == 0u &&
           access != (USER_VM_WRITE | USER_VM_EXEC);
}

static uint64_t access_flags(uint32_t access)
{
    return VMM_PRESENT | VMM_USER |
           ((access & USER_VM_WRITE) != 0u ? VMM_WRITABLE : 0u) |
           ((access & USER_VM_EXEC) == 0u ? VMM_NX : 0u);
}

static bool ready(const struct user_vm *vm)
{
    return vm != NULL && vm->initialized && vm->space.initialized &&
           vm->space.nx_enabled && vm->space.ops.context == vm &&
           vm->page_count <= USER_VM_PAGE_LIMIT &&
           vm->table_count != 0u && vm->table_count <= USER_VM_TABLE_LIMIT &&
           vm->tables[0].allocated && vm->tables[0].published &&
           vm->tables[0].phys == vm->space.root_phys;
}

static size_t page_slot(const struct user_vm *vm, uint64_t virt)
{
    const uint64_t page = memory_align_down(virt);
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; ++i) {
        if (vm->pages[i].allocated && vm->pages[i].virt == page) {
            return i;
        }
    }
    return USER_VM_PAGE_LIMIT;
}

static size_t table_slot(const struct user_vm *vm, uint64_t phys)
{
    for (size_t i = 0u; i < USER_VM_TABLE_LIMIT; ++i) {
        if (vm->tables[i].allocated && vm->tables[i].phys == phys) {
            return i;
        }
    }
    return USER_VM_TABLE_LIMIT;
}

static uint64_t *table_access(void *context, uint64_t phys)
{
    struct user_vm *vm = context;
    void *pointer;
    if (table_slot(vm, phys) == USER_VM_TABLE_LIMIT ||
        !pmm_is_allocated_page(phys) ||
        !memory_phys_to_virt(phys, (size_t)MEMORY_PAGE_SIZE, &pointer)) {
        return NULL;
    }
    return pointer;
}

static bool table_allocate(void *context, uint64_t *out_phys)
{
    struct user_vm *vm = context;
    if (vm->table_count >= USER_VM_TABLE_LIMIT) {
        return false;
    }
    size_t slot = 1u;
    while (slot < USER_VM_TABLE_LIMIT && vm->tables[slot].allocated) {
        ++slot;
    }
    uint64_t phys;
    if (slot == USER_VM_TABLE_LIMIT || !pmm_alloc_page(&phys)) {
        return false;
    }
    /* Reserve ownership before publication; commit must never allocate/fail. */
    vm->tables[slot] = (struct user_vm_table){.phys = phys, .allocated = true};
    ++vm->table_count;
    *out_phys = phys;
    return true;
}

static void release_frame(uint64_t phys)
{
    if (!pmm_free_page(phys)) {
        PANIC("User VM frame ownership mismatch");
    }
}

static void table_release(void *context, uint64_t phys)
{
    struct user_vm *vm = context;
    const size_t slot = table_slot(vm, phys);
    if (slot == 0u || slot == USER_VM_TABLE_LIMIT ||
        vm->tables[slot].published) {
        PANIC("User VM table rollback invariant");
    }
    release_frame(phys);
    vm->tables[slot] = (struct user_vm_table){0};
    --vm->table_count;
}

static void table_commit(void *context, uint64_t phys)
{
    struct user_vm *vm = context;
    const size_t slot = table_slot(vm, phys);
    if (slot == 0u || slot == USER_VM_TABLE_LIMIT ||
        vm->tables[slot].published) {
        PANIC("User VM table commit invariant");
    }
    /* Deliberately NOT pmm_pin_page: the process must reclaim these tables. */
    vm->tables[slot].published = true;
}

static void invalidate(void *context, uint64_t virt)
{
    const struct user_vm *vm = context;
    if ((cpu_read_cr3() & vm->space.physical_mask) == vm->space.root_phys) {
        cpu_invlpg(virt);
    }
}

bool user_vm_create(struct user_vm *vm)
{
    const uint64_t saved = cpu_irq_save();
    struct vmm_info info;
    uint64_t upper[256];
    if (vm == NULL || vm->initialized || vm->space.initialized ||
        vm->page_count != 0u || vm->table_count != 0u ||
        !vmm_get_info(&info) || !info.nx_enabled ||
        (cpu_read_cr4() & (UTAMO_CR4_LA57 | UTAMO_CR4_PCIDE)) != 0u ||
        !vmm_copy_kernel_half(upper)) {
        cpu_irq_restore(saved);
        return false;
    }
    uint64_t phys;
    if (!pmm_alloc_page(&phys)) {
        cpu_irq_restore(saved);
        return false;
    }
    vm->tables[0] = (struct user_vm_table){
        .phys = phys, .allocated = true, .published = true
    };
    vm->table_count = 1u;
    uint64_t *const root = table_access(vm, phys);
    if (root == NULL) {
        release_frame(phys);
        *vm = (struct user_vm){0};
        cpu_irq_restore(saved);
        return false;
    }
    memset(root, 0, (size_t)MEMORY_PAGE_SIZE);
    for (size_t i = 0u; i < 256u; ++i) {
        root[256u + i] = upper[i] & ~VMM_USER;
    }
    const struct vmm_ops ops = {
        .context = vm, .table = table_access, .alloc = table_allocate,
        .free = table_release, .commit = table_commit, .invalidate = invalidate
    };
    if (!vmm_space_init(&vm->space, phys, info.physical_bits, true, &ops)) {
        release_frame(phys);
        *vm = (struct user_vm){0};
        cpu_irq_restore(saved);
        return false;
    }
    vm->initialized = true;
    cpu_irq_restore(saved);
    return true;
}

static bool query_locked(const struct user_vm *vm, uint64_t virt,
                          struct vmm_mapping *out)
{
    struct vmm_mapping mapping;
    if (!ready(vm) || out == NULL || !valid_address(virt) ||
        !vmm_space_query(&vm->space, virt, &mapping)) {
        return false;
    }
    const size_t slot = page_slot(vm, virt);
    if (!mapping.mapped) {
        if (slot != USER_VM_PAGE_LIMIT) {
            return false;
        }
    } else {
        if (slot == USER_VM_PAGE_LIMIT ||
            mapping.page_size != MEMORY_PAGE_SIZE ||
            memory_align_down(mapping.physical) != vm->pages[slot].phys ||
            !pmm_is_allocated_page(vm->pages[slot].phys) ||
            (mapping.flags & (VMM_PRESENT | VMM_USER)) != (VMM_PRESENT | VMM_USER) ||
            (mapping.flags & ~USER_VM_LEAF_FLAGS) != 0u ||
            ((mapping.flags & VMM_WRITABLE) != 0u &&
             (mapping.flags & VMM_NX) == 0u)) {
            return false;
        }
    }
    *out = mapping;
    return true;
}

bool user_vm_query(const struct user_vm *vm, uint64_t virt,
                    struct vmm_mapping *out)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = query_locked(vm, virt, out);
    cpu_irq_restore(saved);
    return result;
}

bool user_vm_alloc_page(struct user_vm *vm, uint64_t virt, uint32_t access)
{
    const uint64_t saved = cpu_irq_save();
    struct vmm_mapping mapping;
    if (!ready(vm) || !memory_is_page_aligned(virt) || !valid_access(access) ||
        vm->page_count == USER_VM_PAGE_LIMIT ||
        !query_locked(vm, virt, &mapping) || mapping.mapped) {
        cpu_irq_restore(saved);
        return false;
    }
    size_t slot = 0u;
    while (slot < USER_VM_PAGE_LIMIT && vm->pages[slot].allocated) {
        ++slot;
    }
    uint64_t phys;
    if (slot == USER_VM_PAGE_LIMIT || !pmm_alloc_page(&phys)) {
        cpu_irq_restore(saved);
        return false;
    }
    void *pointer;
    if (!memory_phys_to_virt(phys, (size_t)MEMORY_PAGE_SIZE, &pointer)) {
        release_frame(phys);
        cpu_irq_restore(saved);
        return false;
    }
    memset(pointer, 0, (size_t)MEMORY_PAGE_SIZE);
    if (!vmm_space_map(&vm->space, virt, phys, access_flags(access))) {
        release_frame(phys);
        cpu_irq_restore(saved);
        return false;
    }
    vm->pages[slot] = (struct user_vm_page){
        .virt = virt, .phys = phys, .allocated = true
    };
    ++vm->page_count;
    cpu_irq_restore(saved);
    return true;
}

bool user_vm_unmap_page(struct user_vm *vm, uint64_t virt)
{
    const uint64_t saved = cpu_irq_save();
    struct vmm_mapping mapping;
    if (!memory_is_page_aligned(virt) ||
        !query_locked(vm, virt, &mapping) || !mapping.mapped ||
        !vmm_space_unmap(&vm->space, virt)) {
        cpu_irq_restore(saved);
        return false;
    }
    const size_t slot = page_slot(vm, virt);
    release_frame(vm->pages[slot].phys);
    vm->pages[slot] = (struct user_vm_page){0};
    --vm->page_count;
    cpu_irq_restore(saved);
    return true;
}

bool user_vm_protect_page(struct user_vm *vm, uint64_t virt, uint32_t access)
{
    const uint64_t saved = cpu_irq_save();
    struct vmm_mapping mapping;
    const bool result = memory_is_page_aligned(virt) && valid_access(access) &&
        query_locked(vm, virt, &mapping) && mapping.mapped &&
        vmm_space_protect(&vm->space, virt, access_flags(access));
    cpu_irq_restore(saved);
    return result;
}

struct copy_chunk {
    void *alias;
    size_t bytes;
};

static bool copy_preflight(const struct user_vm *vm, uint64_t virt, size_t bytes,
                            bool write, struct copy_chunk *chunks,
                            size_t *out_count)
{
    if (!ready(vm) || bytes > USER_VM_COPY_LIMIT) {
        return false;
    }
    if (bytes == 0u) {
        *out_count = 0u;
        return true;
    }
    if (!valid_address(virt) || (uint64_t)bytes > USER_VM_END - virt) {
        return false;
    }
    size_t count = 0u;
    size_t remaining = bytes;
    while (remaining != 0u) {
        struct vmm_mapping mapping;
        if (count == USER_VM_COPY_CHUNKS ||
            !query_locked(vm, virt, &mapping) || !mapping.mapped ||
            (write && (mapping.flags & VMM_WRITABLE) == 0u)) {
            return false;
        }
        size_t chunk = (size_t)(MEMORY_PAGE_SIZE -
                                 (virt & (MEMORY_PAGE_SIZE - 1u)));
        if (chunk > remaining) {
            chunk = remaining;
        }
        void *alias;
        if (!memory_phys_to_virt(mapping.physical, chunk, &alias)) {
            return false;
        }
        chunks[count++] = (struct copy_chunk){.alias = alias, .bytes = chunk};
        remaining -= chunk;
        virt += (uint64_t)chunk;
    }
    *out_count = count;
    return true;
}

bool user_vm_copy_from(const struct user_vm *vm, void *kernel_dst,
                       uint64_t user_src, size_t bytes)
{
    const uint64_t saved = cpu_irq_save();
    struct copy_chunk chunks[USER_VM_COPY_CHUNKS];
    size_t count;
    if ((bytes != 0u && kernel_dst == NULL) ||
        !copy_preflight(vm, user_src, bytes, false, chunks, &count)) {
        cpu_irq_restore(saved);
        return false;
    }
    unsigned char *destination = kernel_dst;
    for (size_t i = 0u; i < count; ++i) {
        memcpy(destination, chunks[i].alias, chunks[i].bytes);
        destination += chunks[i].bytes;
    }
    cpu_irq_restore(saved);
    return true;
}

bool user_vm_copy_to(struct user_vm *vm, uint64_t user_dst,
                     const void *kernel_src, size_t bytes)
{
    const uint64_t saved = cpu_irq_save();
    struct copy_chunk chunks[USER_VM_COPY_CHUNKS];
    size_t count;
    if ((bytes != 0u && kernel_src == NULL) ||
        !copy_preflight(vm, user_dst, bytes, true, chunks, &count)) {
        cpu_irq_restore(saved);
        return false;
    }
    const unsigned char *source = kernel_src;
    for (size_t i = 0u; i < count; ++i) {
        memcpy(chunks[i].alias, source, chunks[i].bytes);
        source += chunks[i].bytes;
    }
    cpu_irq_restore(saved);
    return true;
}

static bool ownership_valid(const struct user_vm *vm)
{
    size_t tables = 0u;
    size_t pages = 0u;
    for (size_t i = 0u; i < USER_VM_TABLE_LIMIT; ++i) {
        if (!vm->tables[i].allocated) {
            continue;
        }
        const uint64_t phys = vm->tables[i].phys;
        if (!vm->tables[i].published || !pmm_is_allocated_page(phys)) {
            return false;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (vm->tables[j].allocated && vm->tables[j].phys == phys) {
                return false;
            }
        }
        ++tables;
    }
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; ++i) {
        if (!vm->pages[i].allocated) {
            continue;
        }
        const uint64_t virt = vm->pages[i].virt;
        const uint64_t phys = vm->pages[i].phys;
        struct vmm_mapping mapping;
        if (!memory_is_page_aligned(virt) ||
            table_slot(vm, phys) != USER_VM_TABLE_LIMIT ||
            !query_locked(vm, virt, &mapping) || !mapping.mapped ||
            mapping.physical != phys) {
            return false;
        }
        for (size_t j = 0u; j < i; ++j) {
            if (vm->pages[j].allocated &&
                (vm->pages[j].phys == phys || vm->pages[j].virt == virt)) {
                return false;
            }
        }
        ++pages;
    }
    return tables == vm->table_count && pages == vm->page_count;
}

bool user_vm_destroy(struct user_vm *vm)
{
    const uint64_t saved = cpu_irq_save();
    if (!ready(vm) ||
        (cpu_read_cr3() & vm->space.physical_mask) == vm->space.root_phys ||
        !ownership_valid(vm)) {
        cpu_irq_restore(saved);
        return false;
    }
    /* No CPU can walk this detached, inactive root. No shared upper table is
     * in our ledger. Reclaim only exclusive frames; release PML4 last.
     */
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; ++i) {
        if (vm->pages[i].allocated) {
            release_frame(vm->pages[i].phys);
        }
    }
    for (size_t i = USER_VM_TABLE_LIMIT; i > 1u; --i) {
        if (vm->tables[i - 1u].allocated) {
            release_frame(vm->tables[i - 1u].phys);
        }
    }
    release_frame(vm->tables[0].phys);
    *vm = (struct user_vm){0};
    cpu_irq_restore(saved);
    return true;
}
