/* SPDX-License-Identifier: MIT */
/* Table storage is simulated host RAM; callbacks never execute privileged code. */
#include <utamo/vmm_core.h>
#include <utamo/memory.h>
#include <utamo/string.h>
#include <stdio.h>

#define MODEL_TABLES 32u
#define FLAGS_RW (VMM_PRESENT | VMM_WRITABLE)
#define FLAGS_ALL (FLAGS_RW | VMM_USER)
#define LARGE_PAT (UINT64_C(1) << 12u)

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

struct model_page {
    uint64_t entries[512];
    bool used;
    bool pinned;
};
struct model {
    struct model_page pages[MODEL_TABLES];
    uint64_t base;
    unsigned int allocations;
    unsigned int frees;
    unsigned int commits;
    unsigned int invalidations;
    unsigned int accesses;
    unsigned int fail_alloc;
    unsigned int fail_access;
    uint64_t last_invalidated;
};
static struct model model;

static uint64_t page_phys(const struct model *fixture, unsigned int index)
{
    return fixture->base + (uint64_t)index * MEMORY_PAGE_SIZE;
}

static unsigned int page_number(const struct model *fixture, uint64_t phys)
{
    if (phys < fixture->base || !memory_is_page_aligned(phys) ||
        phys - fixture->base >= MODEL_TABLES * MEMORY_PAGE_SIZE) {
        return MODEL_TABLES;
    }
    return (unsigned int)((phys - fixture->base) / MEMORY_PAGE_SIZE);
}

static uint64_t *table_access(void *context, uint64_t phys)
{
    struct model *fixture = context;
    ++fixture->accesses;
    if (fixture->fail_access != 0 && fixture->accesses == fixture->fail_access) {
        return NULL;
    }
    const unsigned int index = page_number(fixture, phys);
    return index < MODEL_TABLES && fixture->pages[index].used ?
           fixture->pages[index].entries : NULL;
}

static bool alloc_table(void *context, uint64_t *out_phys)
{
    struct model *fixture = context;
    ++fixture->allocations;
    if (fixture->fail_alloc != 0 &&
        fixture->allocations == fixture->fail_alloc) {
        return false;
    }
    for (unsigned int i = 1; i < MODEL_TABLES; ++i) {
        if (!fixture->pages[i].used) {
            fixture->pages[i].used = true;
            fixture->pages[i].pinned = false;
            (void)memset(fixture->pages[i].entries, 0xa5,
                         sizeof(fixture->pages[i].entries));
            *out_phys = page_phys(fixture, i);
            return true;
        }
    }
    return false;
}

static void free_table(void *context, uint64_t phys)
{
    struct model *fixture = context;
    const unsigned int index = page_number(fixture, phys);
    CHECK(index > 0 && index < MODEL_TABLES);
    if (index > 0 && index < MODEL_TABLES) {
        CHECK(fixture->pages[index].used && !fixture->pages[index].pinned);
        fixture->pages[index].used = false;
        ++fixture->frees;
    }
}

static void commit_table(void *context, uint64_t phys)
{
    struct model *fixture = context;
    const unsigned int index = page_number(fixture, phys);
    CHECK(index > 0 && index < MODEL_TABLES);
    if (index > 0 && index < MODEL_TABLES) {
        CHECK(fixture->pages[index].used && !fixture->pages[index].pinned);
        fixture->pages[index].pinned = true;
        ++fixture->commits;
    }
    bool published = false;
    for (size_t i = 0; i < 512; ++i) {
        published = published || fixture->pages[0].entries[i] != 0;
    }
    CHECK(published);
}

static void invalidate_page(void *context, uint64_t virt)
{
    struct model *fixture = context;
    ++fixture->invalidations;
    fixture->last_invalidated = virt;
    CHECK(memory_is_page_aligned(virt));
}

static struct vmm_ops model_ops(void)
{
    return (struct vmm_ops){
        .context = &model,
        .table = table_access,
        .alloc = alloc_table,
        .free = free_table,
        .commit = commit_table,
        .invalidate = invalidate_page
    };
}

static void model_reset(uint64_t base)
{
    (void)memset(&model, 0, sizeof(model));
    model.base = base;
    model.pages[0].used = true;
    model.pages[0].pinned = true;
}

static void start_space(struct vmm_space *space, bool nx)
{
    model_reset(MEMORY_PAGE_SIZE);
    const struct vmm_ops ops = model_ops();
    CHECK(vmm_space_init(space, model.base, 48, nx, &ops));
    model.accesses = 0;
}

static unsigned int live_tables(void)
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < MODEL_TABLES; ++i) {
        count += model.pages[i].used ? 1u : 0u;
    }
    return count;
}

static void check_root_empty(void)
{
    bool empty = true;
    for (size_t i = 0; i < 512; ++i) {
        empty = empty && model.pages[0].entries[i] == 0;
    }
    CHECK(empty);
}

static uint64_t *raw_entry(uint64_t virt, unsigned int target_level)
{
    unsigned int page = 0;
    for (unsigned int level = 4; level > target_level; --level) {
        const uint64_t entry = model.pages[page].entries[memory_page_index(virt, level)];
        page = page_number(&model, entry & VMM_ADDRESS_MASK);
        if (page >= MODEL_TABLES) {
            return NULL;
        }
    }
    return &model.pages[page].entries[memory_page_index(virt, target_level)];
}

static void test_init_and_inputs(void)
{
    struct vmm_space space = {0};
    model_reset(MEMORY_PAGE_SIZE);
    struct vmm_ops ops = model_ops();
    CHECK(!vmm_space_init(NULL, model.base, 48, true, &ops));
    CHECK(!vmm_space_init(&space, model.base, 48, true, NULL));
    CHECK(!vmm_space_init(&space, model.base + 1u, 48, true, &ops));
    CHECK(!vmm_space_init(&space, UINT64_C(1) << 48u, 48, true, &ops));
    CHECK(!vmm_space_init(&space, model.base, 31, true, &ops));
    CHECK(!vmm_space_init(&space, model.base, 53, true, &ops));
    CHECK(!vmm_space_init(&space, model.base + MEMORY_PAGE_SIZE, 48, true, &ops));
    struct vmm_ops bad = ops;
    bad.table = NULL;
    CHECK(!vmm_space_init(&space, model.base, 48, true, &bad));
    bad = ops;
    bad.alloc = NULL;
    CHECK(!vmm_space_init(&space, model.base, 48, true, &bad));
    bad = ops;
    bad.free = NULL;
    CHECK(!vmm_space_init(&space, model.base, 48, true, &bad));
    bad = ops;
    bad.commit = NULL;
    CHECK(!vmm_space_init(&space, model.base, 48, true, &bad));
    bad = ops;
    bad.invalidate = NULL;
    CHECK(!vmm_space_init(&space, model.base, 48, true, &bad));
    CHECK(!space.initialized);

    start_space(&space, true);
    struct vmm_mapping mapping = {.mapped = true, .physical = 123, .flags = 456,
                                  .page_size = 789};
    CHECK(!vmm_space_query(NULL, 0, &mapping));
    CHECK(!vmm_space_query(&space, 0, NULL));
    CHECK(!vmm_space_query(&space, UINT64_C(0x0000800000000000), &mapping));
    CHECK(mapping.mapped && mapping.physical == 123 && mapping.flags == 456);
    CHECK(mapping.page_size == 789);
    CHECK(vmm_space_query(&space, VMM_TEST_BASE, &mapping));
    CHECK(!mapping.mapped && mapping.physical == 0 && mapping.flags == 0);
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE + 1u, 0, FLAGS_RW));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 1, FLAGS_RW));
    CHECK(!vmm_space_map(&space, UINT64_C(0xffff7ffffffff000), 0, FLAGS_RW));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, UINT64_C(1) << 48u, FLAGS_RW));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, VMM_WRITABLE));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW | VMM_ACCESSED));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW | VMM_DIRTY));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW | VMM_HUGE));
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW | (UINT64_C(1) << 52u)));
    CHECK(!vmm_space_unmap(&space, VMM_TEST_BASE));
    CHECK(!vmm_space_protect(&space, VMM_TEST_BASE, FLAGS_RW));
    CHECK(model.allocations == 0 && model.invalidations == 0);
    check_root_empty();

    start_space(&space, false);
    CHECK(!vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW | VMM_NX));
    CHECK(vmm_space_map(&space, VMM_TEST_BASE, 0, FLAGS_RW));
    CHECK(!vmm_space_protect(&space, VMM_TEST_BASE, FLAGS_RW | VMM_NX));
}

static void test_map_protect_unmap(void)
{
    struct vmm_space space;
    start_space(&space, true);
    const uint64_t virt = VMM_TEST_BASE;
    const uint64_t phys = UINT64_C(0x12345000);
    const uint64_t flags = FLAGS_RW | VMM_NX | VMM_WRITE_THROUGH |
                           VMM_CACHE_DISABLE | VMM_GLOBAL;
    CHECK(vmm_space_map(&space, virt, phys, flags));
    CHECK(model.allocations == 3 && model.commits == 3 && model.frees == 0);
    CHECK(live_tables() == 4);
    CHECK(model.invalidations == 1 && model.last_invalidated == virt);
    struct vmm_mapping mapping;
    CHECK(vmm_space_query(&space, virt + 177u, &mapping));
    CHECK(mapping.mapped && mapping.physical == phys + 177u);
    CHECK(mapping.page_size == MEMORY_PAGE_SIZE && mapping.flags == flags);
    CHECK(!vmm_space_map(&space, virt, phys + MEMORY_PAGE_SIZE, flags));
    CHECK(model.invalidations == 1 && model.allocations == 3);
    CHECK(vmm_space_query(&space, virt + MEMORY_PAGE_SIZE, &mapping));
    CHECK(!mapping.mapped); /* New table pages were zeroed, not allocator poison. */
    CHECK(vmm_space_map(&space, virt + MEMORY_PAGE_SIZE, phys + MEMORY_PAGE_SIZE,
                        FLAGS_ALL));
    CHECK(model.allocations == 3 && model.commits == 3);
    CHECK(vmm_space_query(&space, virt + MEMORY_PAGE_SIZE, &mapping));
    CHECK(mapping.flags == FLAGS_ALL);

    uint64_t *leaf = raw_entry(virt, 1);
    CHECK(leaf != NULL);
    if (leaf != NULL) {
        *leaf |= VMM_ACCESSED | VMM_DIRTY | VMM_HUGE | (UINT64_C(1) << 52u);
    }
    CHECK(vmm_space_protect(&space, virt, VMM_PRESENT | VMM_NX));
    CHECK(vmm_space_query(&space, virt + 4095u, &mapping));
    CHECK(mapping.physical == phys + 4095u && mapping.page_size == 4096);
    CHECK(mapping.flags == (VMM_PRESENT | VMM_NX | VMM_ACCESSED |
          VMM_DIRTY | VMM_HUGE | (UINT64_C(1) << 52u)));
    CHECK(vmm_space_query(&space, virt + MEMORY_PAGE_SIZE, &mapping));
    CHECK(mapping.flags == FLAGS_ALL); /* Sibling permissions never change. */
    CHECK(vmm_space_unmap(&space, virt));
    CHECK(vmm_space_query(&space, virt, &mapping) && !mapping.mapped);
    CHECK(model.frees == 0 && model.commits == 3 && live_tables() == 4);
    CHECK(!vmm_space_unmap(&space, virt));
    CHECK(vmm_space_map(&space, virt, phys, FLAGS_RW));
    CHECK(model.allocations == 3 && model.commits == 3);
    CHECK(vmm_space_unmap(&space, virt + MEMORY_PAGE_SIZE));
    CHECK(vmm_space_unmap(&space, virt));
    CHECK(model.invalidations == 7);
    CHECK(live_tables() == 4 && model.frees == 0); /* Empty tables stay pinned. */
}

static void test_parent_permissions(void)
{
    struct vmm_space space;
    struct vmm_mapping mapping;
    const uint64_t virt = VMM_TEST_BASE;
    start_space(&space, true);
    CHECK(vmm_space_map(&space, virt, UINT64_C(0x400000), FLAGS_ALL));
    uint64_t *parent = raw_entry(virt, 3);
    CHECK(parent != NULL);
    if (parent == NULL) {
        return;
    }
    const uint64_t original = *parent;
    *parent &= ~VMM_WRITABLE;
    CHECK(vmm_space_query(&space, virt, &mapping));
    CHECK((mapping.flags & VMM_WRITABLE) == 0);
    CHECK(!vmm_space_protect(&space, virt, FLAGS_ALL));
    CHECK(!vmm_space_map(&space, virt + MEMORY_PAGE_SIZE, 0, FLAGS_RW));
    CHECK(*parent == (original & ~VMM_WRITABLE));
    CHECK(vmm_space_map(&space, virt + MEMORY_PAGE_SIZE, 0, VMM_PRESENT));
    CHECK(vmm_space_protect(&space, virt, VMM_PRESENT));
    *parent = original & ~VMM_USER;
    CHECK(!vmm_space_protect(&space, virt, FLAGS_ALL));
    CHECK(!vmm_space_map(&space, virt + 2u * MEMORY_PAGE_SIZE, 0,
                         VMM_PRESENT | VMM_USER));
    CHECK(vmm_space_protect(&space, virt, FLAGS_RW));
    *parent = original | VMM_NX;
    CHECK(vmm_space_query(&space, virt, &mapping));
    CHECK((mapping.flags & VMM_NX) != 0);
    CHECK(!vmm_space_protect(&space, virt, FLAGS_RW));
    CHECK(!vmm_space_map(&space, virt + 2u * MEMORY_PAGE_SIZE, 0, FLAGS_RW));
    CHECK(vmm_space_protect(&space, virt, FLAGS_RW | VMM_NX));
    CHECK(vmm_space_map(&space, virt + 2u * MEMORY_PAGE_SIZE, 0, FLAGS_RW | VMM_NX));
    CHECK(*parent == (original | VMM_NX));
}

static void install_huge(uint64_t virt, unsigned int level, uint64_t entry)
{
    unsigned int page = 0;
    for (unsigned int current = 4; current > level; --current) {
        const unsigned int next = page + 1u;
        model.pages[next].used = true;
        model.pages[next].pinned = true;
        model.pages[page].entries[memory_page_index(virt, current)] =
            page_phys(&model, next) | FLAGS_ALL;
        page = next;
    }
    model.pages[page].entries[memory_page_index(virt, level)] = entry;
}

static void test_huge_and_reserved_bits(void)
{
    struct vmm_space space;
    struct vmm_mapping mapping;
    const uint64_t virt = VMM_TEST_BASE;
    start_space(&space, true);
    install_huge(virt, 3, UINT64_C(0x80000000) | FLAGS_ALL | VMM_HUGE | LARGE_PAT);
    CHECK(vmm_space_query(&space, virt + UINT64_C(0x123456), &mapping));
    CHECK(mapping.physical == UINT64_C(0x80123456));
    CHECK(mapping.page_size == (UINT64_C(1) << 30u));
    CHECK((mapping.flags & (VMM_HUGE | LARGE_PAT)) == (VMM_HUGE | LARGE_PAT));
    CHECK(!vmm_space_map(&space, virt, 0, FLAGS_RW));
    CHECK(!vmm_space_unmap(&space, virt));
    CHECK(!vmm_space_protect(&space, virt, VMM_PRESENT));
    CHECK(model.invalidations == 0 && model.allocations == 0);
    uint64_t *leaf = raw_entry(virt, 3);
    CHECK(leaf != NULL);
    if (leaf != NULL) {
        *leaf |= UINT64_C(1) << 29u;
    }
    CHECK(!vmm_space_query(&space, virt, &mapping)); /* 1-GiB reserved address bit. */

    start_space(&space, true);
    install_huge(virt, 2, UINT64_C(0x12200000) | FLAGS_RW | VMM_HUGE | VMM_NX |
                            LARGE_PAT);
    CHECK(vmm_space_query(&space, virt + UINT64_C(0x1fffff), &mapping));
    CHECK(mapping.physical == UINT64_C(0x123fffff));
    CHECK(mapping.page_size == (UINT64_C(1) << 21u));
    CHECK((mapping.flags & (VMM_HUGE | VMM_NX | LARGE_PAT)) ==
          (VMM_HUGE | VMM_NX | LARGE_PAT));
    leaf = raw_entry(virt, 2);
    CHECK(leaf != NULL);
    if (leaf != NULL) {
        *leaf |= UINT64_C(1) << 13u;
    }
    CHECK(!vmm_space_query(&space, virt, &mapping));

    start_space(&space, true);
    model.pages[0].entries[memory_page_index(virt, 4)] = FLAGS_RW | VMM_HUGE;
    CHECK(!vmm_space_query(&space, virt, &mapping)); /* PML4 PS is reserved. */
    start_space(&space, true);
    CHECK(vmm_space_map(&space, virt, 0, FLAGS_RW));
    for (unsigned int level = 1; level <= 4; ++level) {
        uint64_t *entry = raw_entry(virt, level);
        CHECK(entry != NULL);
        if (entry == NULL) {
            continue;
        }
        const uint64_t old = *entry;
        *entry |= UINT64_C(1) << 48u;
        CHECK(!vmm_space_query(&space, virt, &mapping));
        CHECK(!vmm_space_unmap(&space, virt));
        *entry = old;
    }
    start_space(&space, false);
    CHECK(vmm_space_map(&space, virt, 0, FLAGS_RW));
    for (unsigned int level = 1; level <= 4; ++level) {
        uint64_t *entry = raw_entry(virt, level);
        CHECK(entry != NULL);
        if (entry != NULL) {
            const uint64_t old = *entry;
            *entry |= VMM_NX;
            CHECK(!vmm_space_query(&space, virt, &mapping));
            *entry = old;
        }
    }
    start_space(&space, true);
    model.pages[0].entries[memory_page_index(virt, 4)] = UINT64_C(0x800);
    CHECK(vmm_space_query(&space, virt, &mapping) && !mapping.mapped);
    CHECK(!vmm_space_map(&space, virt, 0, FLAGS_RW)); /* Keep non-present metadata. */
    CHECK(model.pages[0].entries[memory_page_index(virt, 4)] == UINT64_C(0x800));
    model.pages[0].entries[memory_page_index(virt, 4)] = model.base | FLAGS_RW;
    CHECK(!vmm_space_query(&space, virt, &mapping)); /* Recursive-table path. */
}

static void test_rollback_and_access_failures(void)
{
    const uint64_t virt = VMM_TEST_BASE;
    for (unsigned int failure = 1; failure <= 3; ++failure) {
        struct vmm_space space;
        start_space(&space, true);
        model.fail_alloc = failure;
        CHECK(!vmm_space_map(&space, virt, 0, FLAGS_RW));
        CHECK(model.allocations == failure && model.frees == failure - 1u);
        CHECK(live_tables() == 1 && model.commits == 0 && model.invalidations == 0);
        check_root_empty();
        model.fail_alloc = 0;
        CHECK(vmm_space_map(&space, virt, 0, FLAGS_RW)); /* All rolled-back pages reusable. */
        CHECK(live_tables() == 4);
    }
    for (unsigned int failure = 2; failure <= 4; ++failure) {
        struct vmm_space space;
        start_space(&space, true);
        model.fail_access = failure;
        CHECK(!vmm_space_map(&space, virt, 0, FLAGS_RW));
        CHECK(model.allocations == failure - 1u && model.frees == failure - 1u);
        CHECK(live_tables() == 1 && model.commits == 0 && model.invalidations == 0);
        check_root_empty();
        model.fail_access = 0;
        CHECK(vmm_space_map(&space, virt, 0, FLAGS_RW));
    }
    struct vmm_space space;
    start_space(&space, true);
    CHECK(vmm_space_map(&space, virt, 0, FLAGS_RW));
    const unsigned int invalidations = model.invalidations;
    for (unsigned int failure = 1; failure <= 4; ++failure) {
        struct vmm_mapping mapping = {.mapped = true, .physical = 123,
                                      .flags = 456, .page_size = 789};
        model.accesses = 0;
        model.fail_access = failure;
        CHECK(!vmm_space_query(&space, virt, &mapping));
        CHECK(mapping.physical == 123 && mapping.flags == 456 &&
              mapping.page_size == 789 && mapping.mapped);
        model.accesses = 0;
        CHECK(!vmm_space_map(&space, virt + MEMORY_PAGE_SIZE, 0, FLAGS_RW));
        model.accesses = 0;
        CHECK(!vmm_space_protect(&space, virt, VMM_PRESENT));
        model.accesses = 0;
        CHECK(!vmm_space_unmap(&space, virt));
        CHECK(model.invalidations == invalidations && live_tables() == 4);
    }
    model.fail_access = 0;
    struct vmm_mapping mapping;
    CHECK(vmm_space_query(&space, virt, &mapping) && mapping.mapped);
}

static void test_high_addresses_and_stress(void)
{
    struct vmm_space space;
    model_reset(UINT64_C(0x100000000));
    struct vmm_ops ops = model_ops();
    CHECK(vmm_space_init(&space, model.base, 52, true, &ops));
    const uint64_t high_phys = UINT64_C(0x000ffffffffff000);
    CHECK(vmm_space_map(&space, UINT64_C(0xfffffffffffff000), high_phys, FLAGS_RW));
    struct vmm_mapping mapping;
    CHECK(vmm_space_query(&space, UINT64_MAX, &mapping));
    CHECK(mapping.physical == UINT64_C(0x000fffffffffffff));
    CHECK(model.commits == 3);

    start_space(&space, true);
    const uint64_t start = VMM_TEST_BASE + (UINT64_C(1) << 30u) -
                           4u * MEMORY_PAGE_SIZE;
    for (unsigned int i = 0; i < 20; ++i) {
        const uint64_t virt = start + (uint64_t)i * MEMORY_PAGE_SIZE;
        const uint64_t phys = UINT64_C(0x100000000) + (uint64_t)i * MEMORY_PAGE_SIZE;
        const uint64_t flags = i % 2u == 0 ? FLAGS_RW | VMM_NX : VMM_PRESENT;
        CHECK(vmm_space_map(&space, virt, phys, flags));
        CHECK(vmm_space_query(&space, virt + 99u, &mapping));
        CHECK(mapping.physical == phys + 99u && mapping.flags == flags);
    }
    CHECK(model.commits == 5); /* Crosses both PT/2-MiB and PD/1-GiB boundaries. */
    const unsigned int allocated = model.allocations;
    for (unsigned int i = 0; i < 20; i += 2u) {
        CHECK(vmm_space_unmap(&space, start + (uint64_t)i * MEMORY_PAGE_SIZE));
    }
    for (unsigned int i = 0; i < 20; ++i) {
        const uint64_t virt = start + (uint64_t)i * MEMORY_PAGE_SIZE;
        CHECK(vmm_space_query(&space, virt, &mapping));
        CHECK(mapping.mapped == (i % 2u != 0));
        if (i % 2u == 0) {
            CHECK(vmm_space_map(&space, virt, UINT64_C(0x800000) +
                  (uint64_t)i * MEMORY_PAGE_SIZE, FLAGS_ALL | VMM_NX));
        }
        CHECK(vmm_space_protect(&space, virt, VMM_PRESENT | VMM_NX));
        CHECK(vmm_space_query(&space, virt, &mapping));
        CHECK(mapping.flags == (VMM_PRESENT | VMM_NX));
    }
    CHECK(model.allocations == allocated && model.frees == 0);
    for (unsigned int i = 0; i < 20; ++i) {
        CHECK(vmm_space_unmap(&space, start + (uint64_t)i * MEMORY_PAGE_SIZE));
    }
    CHECK(model.invalidations == 80 && live_tables() == 6);
}

int main(void)
{
    test_init_and_inputs();
    test_map_protect_unmap();
    test_parent_permissions();
    test_huge_and_reserved_bits();
    test_rollback_and_access_failures();
    test_high_addresses_and_stress();
    (void)printf("UTAMO VMM host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
