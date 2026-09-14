/* SPDX-License-Identifier: MIT */
#include <utamo/vmm_core.h>
#include <utamo/memory.h>
#include <utamo/string.h>

#define VMM_PERMISSION_MASK (VMM_PRESENT | VMM_WRITABLE | VMM_USER | \
    VMM_WRITE_THROUGH | VMM_CACHE_DISABLE | VMM_GLOBAL | VMM_NX)
#define VMM_NEW_PARENT_FLAGS (VMM_PRESENT | VMM_WRITABLE | VMM_USER)
#define VMM_LARGE_PAT (UINT64_C(1) << 12u)

struct walk_result {
    uint64_t *entry;
    uint64_t value;
    unsigned int level;
    bool missing;
    bool parent_writable;
    bool parent_user;
    bool parent_nx;
};

static bool physical_valid(const struct vmm_space *space, uint64_t phys)
{
    return memory_is_page_aligned(phys) &&
           (phys & ~space->physical_mask) == 0;
}

static uint64_t level_page_size(unsigned int level)
{
    return UINT64_C(1) << (12u + 9u * (level - 1u));
}

static bool entry_valid(const struct vmm_space *space, uint64_t entry,
                        unsigned int level)
{
    if ((entry & (VMM_ADDRESS_MASK & ~space->physical_mask)) != 0 ||
        (!space->nx_enabled && (entry & VMM_NX) != 0) ||
        (level == 4u && (entry & VMM_HUGE) != 0)) {
        return false;
    }
    if ((level == 2u || level == 3u) && (entry & VMM_HUGE) != 0) {
        const uint64_t alignment_bits =
            (level_page_size(level) - 1u) & VMM_ADDRESS_MASK & ~VMM_LARGE_PAT;
        if ((entry & alignment_bits) != 0) {
            return false;
        }
    }
    return true;
}

static bool walk(const struct vmm_space *space, uint64_t virt,
                 struct walk_result *result)
{
    if (space == NULL || !space->initialized || !memory_is_canonical(virt)) {
        return false;
    }
    uint64_t phys = space->root_phys;
    uint64_t ancestors[4];
    unsigned int depth = 0;
    bool writable = true;
    bool user = true;
    bool nx = false;
    for (unsigned int level = 4; level != 0; --level) {
        /* Recursive/self-referential tables are outside this bootstrap model. */
        for (unsigned int i = 0; i < depth; ++i) {
            if (ancestors[i] == phys) {
                return false;
            }
        }
        ancestors[depth++] = phys;
        uint64_t *table = space->ops.table(space->ops.context, phys);
        if (table == NULL) {
            return false;
        }
        uint64_t *entry = &table[memory_page_index(virt, level)];
        const uint64_t value = __atomic_load_n(entry, __ATOMIC_ACQUIRE);
        const bool missing = (value & VMM_PRESENT) == 0;
        if (!missing && !entry_valid(space, value, level)) {
            return false;
        }
        if (missing || level == 1u || (value & VMM_HUGE) != 0) {
            *result = (struct walk_result){
                .entry = entry,
                .value = value,
                .level = level,
                .missing = missing,
                .parent_writable = writable,
                .parent_user = user,
                .parent_nx = nx
            };
            return true;
        }
        writable = writable && (value & VMM_WRITABLE) != 0;
        user = user && (value & VMM_USER) != 0;
        nx = nx || (value & VMM_NX) != 0;
        phys = value & space->physical_mask;
    }
    return false;
}

bool vmm_space_init(struct vmm_space *space, uint64_t root_phys,
                    unsigned int physical_bits, bool nx_enabled,
                    const struct vmm_ops *ops)
{
    uint64_t mask;
    if (space == NULL || ops == NULL || ops->table == NULL || ops->alloc == NULL ||
        ops->free == NULL || ops->commit == NULL || ops->invalidate == NULL ||
        !memory_physical_mask(physical_bits, &mask) ||
        !memory_is_page_aligned(root_phys) || (root_phys & ~mask) != 0 ||
        ops->table(ops->context, root_phys) == NULL) {
        return false;
    }
    *space = (struct vmm_space){
        .root_phys = root_phys,
        .physical_mask = mask,
        .nx_enabled = nx_enabled,
        .initialized = true,
        .ops = *ops
    };
    return true;
}

bool vmm_space_query(const struct vmm_space *space, uint64_t virt,
                     struct vmm_mapping *out)
{
    struct walk_result found;
    if (out == NULL || !walk(space, virt, &found)) {
        return false;
    }
    if (found.missing) {
        *out = (struct vmm_mapping){0};
        return true;
    }
    const uint64_t page_size = level_page_size(found.level);
    uint64_t flags = found.value & ~VMM_ADDRESS_MASK;
    if (found.level != 1u) {
        flags |= found.value & VMM_LARGE_PAT;
    }
    if (!found.parent_writable) {
        flags &= ~VMM_WRITABLE;
    }
    if (!found.parent_user) {
        flags &= ~VMM_USER;
    }
    if (found.parent_nx) {
        flags |= VMM_NX;
    }
    *out = (struct vmm_mapping){
        .mapped = true,
        .physical = (found.value & space->physical_mask & ~(page_size - 1u)) |
                    (virt & (page_size - 1u)),
        .flags = flags,
        .page_size = page_size
    };
    return true;
}

static bool flags_valid(const struct vmm_space *space, uint64_t flags)
{
    return space != NULL && space->initialized &&
           (flags & VMM_PRESENT) != 0 && (flags & ~VMM_PERMISSION_MASK) == 0 &&
           (space->nx_enabled || (flags & VMM_NX) == 0);
}

static bool parents_allow(const struct walk_result *found, uint64_t flags)
{
    return (found->parent_writable || (flags & VMM_WRITABLE) == 0) &&
           (found->parent_user || (flags & VMM_USER) == 0) &&
           (!found->parent_nx || (flags & VMM_NX) != 0);
}

static void rollback(const struct vmm_space *space, const uint64_t *frames,
                     unsigned int count)
{
    while (count != 0) {
        --count;
        space->ops.free(space->ops.context, frames[count]);
    }
}

bool vmm_space_map(struct vmm_space *space, uint64_t virt,
                   uint64_t phys, uint64_t flags)
{
    struct walk_result found;
    if (!flags_valid(space, flags) || !physical_valid(space, phys) ||
        !memory_is_page_aligned(virt) || !walk(space, virt, &found) ||
        !found.missing || found.value != 0 || !parents_allow(&found, flags)) {
        return false;
    }
    if (found.level == 1u) {
        __atomic_store_n(found.entry, phys | flags, __ATOMIC_RELEASE);
        space->ops.invalidate(space->ops.context, virt);
        return true;
    }
    uint64_t frames[3];
    uint64_t *tables[3];
    const unsigned int needed = found.level - 1u;
    unsigned int acquired = 0;
    for (unsigned int i = 0; i < needed; ++i) {
        if (!space->ops.alloc(space->ops.context, &frames[i])) {
            rollback(space, frames, acquired);
            return false;
        }
        ++acquired;
        if (!physical_valid(space, frames[i])) {
            rollback(space, frames, acquired);
            return false;
        }
        tables[i] = space->ops.table(space->ops.context, frames[i]);
        if (tables[i] == NULL) {
            rollback(space, frames, acquired);
            return false;
        }
        (void)memset(tables[i], 0, (size_t)MEMORY_PAGE_SIZE);
    }
    /* The entire new subtree is private until the single publishing store.
     * alloc/table failures above leave every pre-existing entry untouched.
     */
    for (unsigned int i = 0; i < needed; ++i) {
        const unsigned int level = found.level - 1u - i;
        const unsigned int index = memory_page_index(virt, level);
        tables[i][index] = level == 1u ? phys | flags :
                          frames[i + 1u] | VMM_NEW_PARENT_FLAGS;
    }
    __atomic_store_n(found.entry, frames[0] | VMM_NEW_PARENT_FLAGS,
                     __ATOMIC_RELEASE);
    for (unsigned int i = 0; i < needed; ++i) {
        space->ops.commit(space->ops.context, frames[i]);
    }
    space->ops.invalidate(space->ops.context, virt);
    return true;
}

bool vmm_space_unmap(struct vmm_space *space, uint64_t virt)
{
    struct walk_result found;
    if (!memory_is_page_aligned(virt) || !walk(space, virt, &found) ||
        found.missing || found.level != 1u) {
        return false;
    }
    __atomic_store_n(found.entry, 0, __ATOMIC_RELEASE);
    space->ops.invalidate(space->ops.context, virt);
    return true;
}

bool vmm_space_protect(struct vmm_space *space, uint64_t virt, uint64_t flags)
{
    struct walk_result found;
    if (!flags_valid(space, flags) || !memory_is_page_aligned(virt) ||
        !walk(space, virt, &found) || found.missing || found.level != 1u ||
        !parents_allow(&found, flags)) {
        return false;
    }
    __atomic_store_n(found.entry, (found.value & ~VMM_PERMISSION_MASK) | flags,
                     __ATOMIC_RELEASE);
    space->ops.invalidate(space->ops.context, virt);
    return true;
}
