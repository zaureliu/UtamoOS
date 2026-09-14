/* SPDX-License-Identifier: MIT */
/* Pure host fixtures: no physical addresses are dereferenced or privileged I/O. */
#include <utamo/heap_pages.h>
#include <utamo/memory.h>

#include <stdio.h>
#include <string.h>

#define FIXTURE_PAGES 24u
#define TEST_HEAP_BASE UINT64_C(0xffffc00001000000)
#define TEST_PHYS_BASE UINT64_C(0x100000)
#define PAGE_BYTES ((size_t)MEMORY_PAGE_SIZE)

static unsigned int checks;
static unsigned int failures;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

struct fixture {
    bool owned[FIXTURE_PAGES];
    unsigned char bytes[FIXTURE_PAGES][4096];
    struct vmm_mapping mappings[FIXTURE_PAGES];
    unsigned int alloc_calls, free_calls, zero_calls;
    unsigned int map_calls, query_calls, unmap_calls;
    unsigned int fail_alloc, fail_free, fail_zero;
    unsigned int fail_map, fail_query, fail_unmap;
    unsigned int stale_query;
};

static size_t phys_index(uint64_t phys)
{
    if (phys < TEST_PHYS_BASE || !memory_is_page_aligned(phys)) {
        return FIXTURE_PAGES;
    }
    const uint64_t index = (phys - TEST_PHYS_BASE) / MEMORY_PAGE_SIZE;
    return index < FIXTURE_PAGES ? (size_t)index : FIXTURE_PAGES;
}

static size_t virt_index(uint64_t virt)
{
    if (virt < TEST_HEAP_BASE || !memory_is_page_aligned(virt)) {
        return FIXTURE_PAGES;
    }
    const uint64_t index = (virt - TEST_HEAP_BASE) / MEMORY_PAGE_SIZE;
    return index < FIXTURE_PAGES ? (size_t)index : FIXTURE_PAGES;
}

static bool alloc_page(void *context, uint64_t *phys)
{
    struct fixture *fixture = context;
    ++fixture->alloc_calls;
    if (fixture->alloc_calls == fixture->fail_alloc) {
        return false;
    }
    for (size_t i = 0u; i < FIXTURE_PAGES; ++i) {
        if (!fixture->owned[i]) {
            fixture->owned[i] = true;
            memset(fixture->bytes[i], 0xa5, sizeof(fixture->bytes[i]));
            *phys = TEST_PHYS_BASE + (uint64_t)i * MEMORY_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

static bool free_page(void *context, uint64_t phys)
{
    struct fixture *fixture = context;
    ++fixture->free_calls;
    if (fixture->free_calls == fixture->fail_free) {
        return false;
    }
    const size_t index = phys_index(phys);
    CHECK(index < FIXTURE_PAGES && fixture->owned[index]);
    if (index >= FIXTURE_PAGES || !fixture->owned[index]) {
        return false;
    }
    for (size_t i = 0u; i < FIXTURE_PAGES; ++i) {
        if (fixture->mappings[i].mapped &&
            fixture->mappings[i].physical == phys) {
            CHECK(false); /* Never release a frame still mapped in the fixture. */
            return false;
        }
    }
    fixture->owned[index] = false;
    return true;
}

static bool zero_page(void *context, uint64_t phys)
{
    struct fixture *fixture = context;
    ++fixture->zero_calls;
    const size_t index = phys_index(phys);
    CHECK(index < FIXTURE_PAGES && fixture->owned[index]);
    if (index >= FIXTURE_PAGES || !fixture->owned[index]) {
        return false;
    }
    if (fixture->zero_calls == fixture->fail_zero) {
        /* Failure can occur after writing a prefix, but ownership is unchanged. */
        fixture->bytes[index][0] = 0u;
        return false;
    }
    memset(fixture->bytes[index], 0, sizeof(fixture->bytes[index]));
    return true;
}

static bool all_zero(const unsigned char *bytes)
{
    for (size_t i = 0u; i < 4096u; ++i) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool map_page(void *context, uint64_t virt, uint64_t phys, uint64_t flags)
{
    struct fixture *fixture = context;
    ++fixture->map_calls;
    if (fixture->map_calls == fixture->fail_map) {
        return false;
    }
    const size_t slot = virt_index(virt);
    const size_t frame = phys_index(phys);
    CHECK(slot < FIXTURE_PAGES && !fixture->mappings[slot].mapped);
    CHECK(frame < FIXTURE_PAGES && fixture->owned[frame]);
    if (slot >= FIXTURE_PAGES || fixture->mappings[slot].mapped ||
        frame >= FIXTURE_PAGES || !fixture->owned[frame]) {
        return false;
    }
    CHECK(all_zero(fixture->bytes[frame])); /* Zero precedes publication. */
    CHECK((flags & ~(VMM_PRESENT | VMM_WRITABLE | VMM_NX)) == 0u);
    CHECK((flags & (VMM_PRESENT | VMM_WRITABLE)) ==
          (VMM_PRESENT | VMM_WRITABLE));
    fixture->mappings[slot] = (struct vmm_mapping){
        .mapped = true, .physical = phys, .flags = flags,
        .page_size = MEMORY_PAGE_SIZE
    };
    return true;
}

static bool query_page(void *context, uint64_t virt, struct vmm_mapping *mapping)
{
    struct fixture *fixture = context;
    ++fixture->query_calls;
    if (fixture->query_calls == fixture->fail_query) {
        return false;
    }
    const size_t slot = virt_index(virt);
    CHECK(slot < FIXTURE_PAGES);
    if (slot >= FIXTURE_PAGES) {
        return false;
    }
    *mapping = fixture->mappings[slot];
    if (fixture->query_calls == fixture->stale_query) {
        mapping->mapped = true; /* Simulate an impossible failed unmap readback. */
    }
    return true;
}

static bool unmap_page(void *context, uint64_t virt)
{
    struct fixture *fixture = context;
    ++fixture->unmap_calls;
    if (fixture->unmap_calls == fixture->fail_unmap) {
        return false;
    }
    const size_t slot = virt_index(virt);
    CHECK(slot < FIXTURE_PAGES && fixture->mappings[slot].mapped);
    if (slot >= FIXTURE_PAGES || !fixture->mappings[slot].mapped) {
        return false;
    }
    fixture->mappings[slot] = (struct vmm_mapping){0};
    return true;
}

static struct heap_page_ops make_ops(struct fixture *fixture)
{
    return (struct heap_page_ops){
        .context = fixture, .alloc = alloc_page, .free = free_page,
        .zero = zero_page, .map = map_page, .query = query_page,
        .unmap = unmap_page
    };
}

static size_t owned_count(const struct fixture *fixture)
{
    size_t count = 0u;
    for (size_t i = 0u; i < FIXTURE_PAGES; ++i) {
        if (fixture->owned[i]) {
            ++count;
        }
    }
    return count;
}

static void prepare(struct heap_pages *state, struct fixture *fixture,
                    size_t prefix_pages, bool nx)
{
    *state = (struct heap_pages){0};
    memset(fixture, 0, sizeof(*fixture));
    const struct heap_page_ops ops = make_ops(fixture);
    CHECK(heap_pages_init(state, TEST_HEAP_BASE,
                           FIXTURE_PAGES * PAGE_BYTES, nx, &ops));
    CHECK(heap_pages_grow(state, 0u, prefix_pages * PAGE_BYTES));
    for (size_t i = 0u; i < prefix_pages; ++i) {
        const size_t frame = phys_index(fixture->mappings[i].physical);
        fixture->bytes[frame][0] = (unsigned char)(0x30u + i);
        fixture->bytes[frame][4095] = (unsigned char)(0x80u + i);
    }
}

static void check_prefix(const struct heap_pages *state,
                         const struct fixture *fixture, size_t prefix_pages)
{
    CHECK(state->mapped_bytes == prefix_pages * PAGE_BYTES);
    for (size_t i = 0u; i < prefix_pages; ++i) {
        CHECK(fixture->mappings[i].mapped);
        CHECK(fixture->mappings[i].physical ==
              TEST_PHYS_BASE + (uint64_t)i * MEMORY_PAGE_SIZE);
        const size_t frame = phys_index(fixture->mappings[i].physical);
        CHECK(frame < FIXTURE_PAGES && fixture->owned[frame]);
        CHECK(fixture->bytes[frame][0] == (unsigned char)(0x30u + i));
        CHECK(fixture->bytes[frame][4095] == (unsigned char)(0x80u + i));
    }
}

static void test_arguments(void)
{
    static struct fixture fixture;
    memset(&fixture, 0, sizeof(fixture));
    struct heap_pages state = {0};
    struct heap_page_ops ops = make_ops(&fixture);
    CHECK(!heap_pages_init(NULL, TEST_HEAP_BASE, PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, PAGE_BYTES, true, NULL));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, 0u, true, &ops));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE + 1u, PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, PAGE_BYTES + 1u, true, &ops));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, SIZE_MAX, true, &ops));
    CHECK(!heap_pages_init(&state, VMM_TEST_BASE, PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, VMM_TEST_BASE + VMM_TEST_SIZE - MEMORY_PAGE_SIZE,
                           2u * PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, UINT64_C(0x0000800000000000),
                           PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, UINT64_C(0xfffffffffffff000),
                           2u * PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, VMM_DYNAMIC_BASE + VMM_DYNAMIC_SIZE,
                           PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state,
                           VMM_DYNAMIC_BASE + VMM_DYNAMIC_SIZE - MEMORY_PAGE_SIZE,
                           2u * PAGE_BYTES, true, &ops));
    CHECK(!state.initialized && state.mapped_bytes == 0u);
    CHECK(!heap_pages_grow(&state, 0u, PAGE_BYTES));
    CHECK(!heap_pages_grow(NULL, 0u, PAGE_BYTES));
    for (unsigned int missing = 0u; missing < 6u; ++missing) {
        ops = make_ops(&fixture);
        switch (missing) {
        case 0u: ops.alloc = NULL; break;
        case 1u: ops.free = NULL; break;
        case 2u: ops.zero = NULL; break;
        case 3u: ops.map = NULL; break;
        case 4u: ops.query = NULL; break;
        default: ops.unmap = NULL; break;
        }
        CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, PAGE_BYTES, true, &ops));
        CHECK(!state.initialized);
    }
    ops = make_ops(&fixture);
    CHECK(heap_pages_init(&state, TEST_HEAP_BASE, 2u * PAGE_BYTES, true, &ops));
    CHECK(!heap_pages_init(&state, TEST_HEAP_BASE, PAGE_BYTES, false, &ops));
    CHECK(state.capacity_bytes == 2u * PAGE_BYTES && state.nx);
    CHECK(!heap_pages_grow(&state, PAGE_BYTES, 2u * PAGE_BYTES));
    CHECK(!heap_pages_grow(&state, 0u, 1u));
    CHECK(!heap_pages_grow(&state, 0u, 3u * PAGE_BYTES));
    CHECK(!heap_pages_grow(&state, 0u, SIZE_MAX));
    CHECK(heap_pages_grow(&state, 0u, 0u));
    CHECK(fixture.alloc_calls == 0u && fixture.query_calls == 0u);
    CHECK(heap_pages_grow(&state, 0u, PAGE_BYTES));
    CHECK(!heap_pages_grow(&state, PAGE_BYTES, 0u));
    CHECK(!heap_pages_grow(&state, 0u, 2u * PAGE_BYTES));
    CHECK(!heap_pages_grow(&state, PAGE_BYTES + 1u, 2u * PAGE_BYTES));
    const unsigned int queries = fixture.query_calls;
    CHECK(heap_pages_grow(&state, PAGE_BYTES, PAGE_BYTES));
    CHECK(fixture.query_calls == queries && owned_count(&fixture) == 1u);
}

static void test_success(void)
{
    static struct fixture fixture;
    struct heap_pages state;
    for (unsigned int nx = 0u; nx < 2u; ++nx) {
        prepare(&state, &fixture, 2u, nx != 0u);
        CHECK(heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
        CHECK(state.mapped_bytes == 8u * PAGE_BYTES && !state.corrupted);
        CHECK(owned_count(&fixture) == 8u);
        for (size_t i = 0u; i < 8u; ++i) {
            CHECK(fixture.mappings[i].mapped);
            CHECK(fixture.mappings[i].flags ==
                  (VMM_PRESENT | VMM_WRITABLE | (nx != 0u ? VMM_NX : 0u)));
            if (i >= 2u) {
                CHECK(all_zero(fixture.bytes[phys_index(fixture.mappings[i].physical)]));
            }
            for (size_t j = 0u; j < i; ++j) {
                CHECK(fixture.mappings[i].physical != fixture.mappings[j].physical);
            }
        }
        CHECK(fixture.bytes[0][0] == 0x30u && fixture.bytes[0][4095] == 0x80u);
        CHECK(fixture.bytes[1][0] == 0x31u && fixture.bytes[1][4095] == 0x81u);
        CHECK(fixture.free_calls == 0u && fixture.unmap_calls == 0u);
        CHECK(heap_pages_grow(&state, 8u * PAGE_BYTES, FIXTURE_PAGES * PAGE_BYTES));
        CHECK(owned_count(&fixture) == FIXTURE_PAGES);
        CHECK(!heap_pages_grow(&state, FIXTURE_PAGES * PAGE_BYTES,
                               (FIXTURE_PAGES + 1u) * PAGE_BYTES));
    }
}

static void test_failure_positions(void)
{
    static struct fixture fixture;
    struct heap_pages state;
    /* Prefix two pages; append six. Fail each stage at every new page. */
    for (unsigned int kind = 0u; kind < 3u; ++kind) {
        for (unsigned int position = 1u; position <= 6u; ++position) {
            prepare(&state, &fixture, 2u, true);
            if (kind == 0u) {
                fixture.fail_alloc = fixture.alloc_calls + position;
            } else if (kind == 1u) {
                fixture.fail_zero = fixture.zero_calls + position;
            } else {
                fixture.fail_map = fixture.map_calls + position;
            }
            CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
            CHECK(!state.corrupted);
            check_prefix(&state, &fixture, 2u);
            CHECK(owned_count(&fixture) == 2u);
            for (size_t i = 2u; i < FIXTURE_PAGES; ++i) {
                CHECK(!fixture.mappings[i].mapped);
            }
            CHECK(fixture.unmap_calls == position - 1u);
            CHECK(fixture.free_calls == position - (kind == 0u ? 1u : 0u));
            fixture.fail_alloc = 0u;
            fixture.fail_zero = 0u;
            fixture.fail_map = 0u;
            CHECK(heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
            CHECK(!state.corrupted && state.mapped_bytes == 8u * PAGE_BYTES);
            CHECK(owned_count(&fixture) == 8u);
        }
    }
}

static void test_preflight(void)
{
    static struct fixture fixture;
    struct heap_pages state;
    for (unsigned int position = 1u; position <= 6u; ++position) {
        prepare(&state, &fixture, 2u, true);
        const unsigned int allocations = fixture.alloc_calls;
        fixture.fail_query = fixture.query_calls + position;
        CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
        CHECK(!state.corrupted && fixture.alloc_calls == allocations);
        CHECK(fixture.free_calls == 0u && fixture.unmap_calls == 0u);
        check_prefix(&state, &fixture, 2u);
        CHECK(owned_count(&fixture) == 2u);
    }
    for (size_t position = 2u; position < 8u; ++position) {
        prepare(&state, &fixture, 2u, true);
        const unsigned int allocations = fixture.alloc_calls;
        fixture.owned[23] = true;
        fixture.bytes[23][0] = 0x7bu;
        fixture.mappings[position] = (struct vmm_mapping){
            .mapped = true, .physical = TEST_PHYS_BASE + 23u * MEMORY_PAGE_SIZE,
            .page_size = MEMORY_PAGE_SIZE, .flags = VMM_PRESENT
        };
        CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
        CHECK(!state.corrupted && fixture.alloc_calls == allocations);
        CHECK(fixture.free_calls == 0u && fixture.unmap_calls == 0u);
        CHECK(fixture.mappings[position].mapped && fixture.owned[23]);
        CHECK(fixture.bytes[23][0] == 0x7bu);
        check_prefix(&state, &fixture, 2u);
        CHECK(owned_count(&fixture) == 3u);
    }
}

static void test_corrupt_rollback(void)
{
    static struct fixture fixture;
    struct heap_pages state;
    /* Two successful new pages, then map failure triggers rollback. */
    for (unsigned int failure = 0u; failure < 5u; ++failure) {
        prepare(&state, &fixture, 2u, true);
        fixture.fail_map = fixture.map_calls + 3u;
        if (failure == 0u) {
            fixture.fail_query = fixture.query_calls + 6u + 1u;
        } else if (failure == 1u) {
            fixture.fail_unmap = 1u;
        } else if (failure == 2u) {
            fixture.fail_free = 2u; /* After freeing the not-yet-mapped frame. */
        } else if (failure == 3u) {
            fixture.stale_query = fixture.query_calls + 6u + 2u;
        } else {
            fixture.fail_free = 1u; /* Current allocated, unmapped frame. */
        }
        CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 8u * PAGE_BYTES));
        CHECK(state.corrupted);
        check_prefix(&state, &fixture, 2u);
        const unsigned int allocations = fixture.alloc_calls;
        const unsigned int queries = fixture.query_calls;
        CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 3u * PAGE_BYTES));
        CHECK(!heap_pages_grow(&state, 2u * PAGE_BYTES, 2u * PAGE_BYTES));
        CHECK(fixture.alloc_calls == allocations && fixture.query_calls == queries);
        CHECK(owned_count(&fixture) == 3u); /* Leak is explicit; wrapper must panic. */
        if (failure <= 1u) {
            CHECK(fixture.mappings[3].mapped);
            CHECK(fixture.owned[3]); /* Failed removal must not release its frame. */
        }
    }
}

int main(void)
{
    test_arguments();
    test_success();
    test_failure_positions();
    test_preflight();
    test_corrupt_rollback();
    (void)printf("UTAMO heap page host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
