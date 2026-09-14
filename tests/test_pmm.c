/* SPDX-License-Identifier: MIT */
/* Host fixtures contain ordinary bytes; no physical addresses are dereferenced. */
#include <utamo/pmm.h>
#include <utamo/memory.h>
#include <utamo/cpu.h>
#include <utamo/string.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

static unsigned char small_storage[4096];
static unsigned char large_storage[270336];
static unsigned char wrapper_storage[4096];
static uint64_t simulated_flags = UINT64_C(0x202);
static unsigned int critical_entries;
static unsigned int critical_exits;

uint64_t cpu_irq_save(void)
{
    const uint64_t old = simulated_flags;
    simulated_flags &= ~UINT64_C(0x200);
    ++critical_entries;
    return old;
}

void cpu_irq_restore(uint64_t flags)
{
    CHECK((simulated_flags & UINT64_C(0x200)) == 0);
    simulated_flags = flags;
    ++critical_exits;
}

static void add_region(struct memory_map *map, uint64_t base, uint64_t length,
                       enum memory_type type)
{
    const struct memory_region region = {base, length, type};
    CHECK(memory_map_add(map, &region));
}

static void small_map(struct memory_map *map)
{
    memory_map_init(map);
    add_region(map, 0, 64u * MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    add_region(map, 64u * MEMORY_PAGE_SIZE, 8u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_RESERVED);
}

static bool start_small(struct pmm_state *state, struct pmm_plan *plan)
{
    struct memory_map map;
    small_map(&map);
    *state = (struct pmm_state){0};
    CHECK(pmm_plan(&map, plan));
    return pmm_core_init(state, &map, plan, small_storage);
}

static void test_plan(void)
{
    struct memory_map map;
    struct pmm_plan plan = {1, 2, 3, 4};
    CHECK(!pmm_plan(NULL, &plan));
    CHECK(plan.storage_phys == 1 && plan.storage_bytes == 2);
    memory_map_init(&map);
    CHECK(!pmm_plan(&map, &plan));
    add_region(&map, 0, MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    CHECK(!pmm_plan(&map, &plan)); /* Only page zero cannot hold metadata. */
    small_map(&map);
    CHECK(!pmm_plan(&map, NULL));
    CHECK(pmm_plan(&map, &plan));
    CHECK(plan.storage_phys == MEMORY_PAGE_SIZE);
    CHECK(plan.storage_bytes == MEMORY_PAGE_SIZE);
    CHECK(plan.bitmap_bytes == 8);
    CHECK(plan.span_frames == 64);

    struct pmm_state state = {0};
    struct pmm_plan altered = plan;
    altered.storage_phys += MEMORY_PAGE_SIZE;
    (void)memset(small_storage, 0xa5, sizeof(small_storage));
    CHECK(!pmm_core_init(NULL, &map, &plan, small_storage));
    CHECK(!pmm_core_init(&state, &map, &altered, small_storage));
    CHECK(!state.initialized && small_storage[0] == 0xa5);
    altered = plan;
    ++altered.bitmap_bytes;
    CHECK(!pmm_core_init(&state, &map, &altered, small_storage));
    altered = plan;
    ++altered.storage_bytes;
    CHECK(!pmm_core_init(&state, &map, &altered, small_storage));
    altered = plan;
    ++altered.span_frames;
    CHECK(!pmm_core_init(&state, &map, &altered, small_storage));
    CHECK(!pmm_core_init(&state, &map, NULL, small_storage));
    CHECK(!pmm_core_init(&state, &map, &plan, NULL));
    CHECK(pmm_core_init(&state, &map, &plan, small_storage));
    CHECK(!pmm_core_init(&state, &map, &plan, small_storage));
    CHECK(small_storage[0] == 0xfc); /* Metadata/page zero ineligible. */
    CHECK(small_storage[8] == 3);    /* Only those two pages occupied. */

    /* Invalid arithmetic is rejected before any metadata write. */
    map.regions[0].base = UINT64_MAX - 2047u;
    map.regions[0].length = 4096;
    CHECK(!pmm_plan(&map, &plan));
    small_map(&map);
    ++map.regions[0].base;
    CHECK(!pmm_plan(&map, &plan));
    small_map(&map);
    --map.usable_bytes;
    CHECK(!pmm_plan(&map, &plan));
    map.count = UTAMO_MEMORY_REGION_LIMIT + 1u;
    CHECK(!pmm_plan(&map, &plan));

    memory_map_init(&map);
    add_region(&map, MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    add_region(&map, UINT64_C(0x100000000), 60u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    CHECK(!pmm_plan(&map, &plan)); /* Neither fragmented range fits metadata. */

    memory_map_init(&map);
    add_region(&map, MEMORY_PAGE_SIZE, 3u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    add_region(&map, UINT64_C(0xffffffffff000000), MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_RESERVED);
    CHECK(pmm_plan(&map, &plan));
    CHECK(plan.span_frames == 4 && plan.bitmap_bytes == 1);
    CHECK(plan.storage_bytes == MEMORY_PAGE_SIZE); /* High MMIO adds no bitmap. */
}

static void test_accounting_and_invalid_free(void)
{
    struct pmm_state state;
    struct pmm_plan plan;
    struct pmm_stats stats;
    CHECK(start_small(&state, &plan));
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.total_frames == 64 && stats.free_frames == 62);
    CHECK(stats.used_frames == 2 && stats.span_frames == 64);
    CHECK(stats.bitmap_phys == MEMORY_PAGE_SIZE && stats.bitmap_bytes == 8);
    CHECK(stats.storage_bytes == MEMORY_PAGE_SIZE);
    CHECK(!pmm_core_is_allocated_page(&state, 0));
    CHECK(!pmm_core_is_allocated_page(&state, plan.storage_phys));
    CHECK(!pmm_core_is_allocated_page(&state, 2u * MEMORY_PAGE_SIZE));
    CHECK(!pmm_core_is_allocated_page(NULL, 0));
    CHECK(!pmm_core_is_allocated_page(&state, UINT64_MAX));
    CHECK(!pmm_core_get_stats(NULL, &stats));
    CHECK(!pmm_core_get_stats(&state, NULL));
    CHECK(!pmm_core_free_pages(&state, 0, 1));
    CHECK(!pmm_core_free_pages(&state, plan.storage_phys, 1));
    CHECK(!pmm_core_free_pages(&state, 2u * MEMORY_PAGE_SIZE, 1));
    CHECK(!pmm_core_free_pages(&state, 64u * MEMORY_PAGE_SIZE, 1));
    CHECK(!pmm_core_free_pages(&state, UINT64_MAX, 1));
    CHECK(!pmm_core_free_pages(&state, 2u * MEMORY_PAGE_SIZE, SIZE_MAX));
    CHECK(!pmm_core_free_pages(&state, 2u * MEMORY_PAGE_SIZE, 0));

    uint64_t page = UINT64_C(0xdeadbeef);
    CHECK(!pmm_core_alloc_pages(&state, 0, &page));
    CHECK(!pmm_core_alloc_pages(&state, SIZE_MAX, &page));
    CHECK(!pmm_core_alloc_pages(&state, 1, NULL));
    CHECK(page == UINT64_C(0xdeadbeef));
    CHECK(pmm_core_alloc_pages(&state, 3, &page));
    CHECK(page == 2u * MEMORY_PAGE_SIZE);
    CHECK(pmm_core_is_allocated_page(&state, page));
    CHECK(pmm_core_is_allocated_page(&state, page + 2u * MEMORY_PAGE_SIZE));
    CHECK(!pmm_core_is_allocated_page(&state, page + 1u));
    CHECK(!pmm_core_free_pages(&state, page + 1u, 1));
    CHECK(!pmm_core_free_pages(&state, page, 4)); /* Includes unallocated page. */
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.free_frames == 59 && stats.used_frames == 5);
    CHECK(pmm_core_free_pages(&state, page, 3));
    CHECK(!pmm_core_is_allocated_page(&state, page));
    CHECK(!pmm_core_free_pages(&state, page, 1)); /* Obvious double free. */
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.free_frames == 62 && stats.used_frames == 2);
    CHECK(!pmm_core_pin_page(&state, page));
    CHECK(!pmm_core_pin_page(&state, 0));
    CHECK(!pmm_core_pin_page(&state, UINT64_MAX));
}

static void test_reservations_and_pinning(void)
{
    struct pmm_state state;
    struct pmm_plan plan;
    struct pmm_stats stats;
    CHECK(start_small(&state, &plan));
    CHECK(!pmm_core_reserve_range(&state, 0, 0));
    CHECK(!pmm_core_reserve_range(&state, UINT64_MAX, 1));
    CHECK(!pmm_core_reserve_range(&state, UINT64_MAX - 4096u, 4096));
    CHECK(pmm_core_reserve_range(&state, 10u * MEMORY_PAGE_SIZE + 3u, 4096));
    CHECK(pmm_core_reserve_range(&state, 10u * MEMORY_PAGE_SIZE, 8192));
    CHECK(pmm_core_reserve_range(&state, 90u * MEMORY_PAGE_SIZE, 4096));
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.free_frames == 60 && stats.used_frames == 4);
    CHECK(!pmm_core_free_pages(&state, 10u * MEMORY_PAGE_SIZE, 1));
    CHECK(!pmm_core_free_pages(&state, 11u * MEMORY_PAGE_SIZE, 1));

    uint64_t page;
    CHECK(pmm_core_alloc_pages(&state, 2, &page));
    CHECK(page == 2u * MEMORY_PAGE_SIZE);
    CHECK(!pmm_core_reserve_range(&state, MEMORY_PAGE_SIZE, 3u * 4096u));
    CHECK(pmm_core_get_stats(&state, &stats) && stats.free_frames == 58);
    CHECK(pmm_core_pin_page(&state, page));
    CHECK(!pmm_core_is_allocated_page(&state, page));
    CHECK(!pmm_core_pin_page(&state, page));
    CHECK(!pmm_core_free_pages(&state, page, 2)); /* Atomic failure, second lives. */
    CHECK(pmm_core_free_pages(&state, page + MEMORY_PAGE_SIZE, 1));
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.free_frames == 59 && stats.used_frames == 5);
    CHECK(pmm_core_reserve_range(&state, page, 4096)); /* Already permanent. */

    CHECK(start_small(&state, &plan));
    CHECK(pmm_core_alloc_pages(&state, 62, &page));
    CHECK(page == 2u * MEMORY_PAGE_SIZE);
    CHECK(!pmm_core_alloc_pages(&state, 1, &page));
    CHECK(pmm_core_get_stats(&state, &stats) && stats.free_frames == 0);
    CHECK(pmm_core_free_pages(&state, 2u * MEMORY_PAGE_SIZE, 62));
    CHECK(pmm_core_get_stats(&state, &stats) && stats.free_frames == 62);
}

static void test_next_fit_and_atomicity(void)
{
    struct pmm_state state;
    struct pmm_plan plan;
    CHECK(start_small(&state, &plan));
    uint64_t first;
    uint64_t second;
    uint64_t combined;
    CHECK(pmm_core_alloc_pages(&state, 20, &first));
    CHECK(pmm_core_alloc_pages(&state, 20, &second));
    CHECK(first == 2u * MEMORY_PAGE_SIZE && second == 22u * MEMORY_PAGE_SIZE);
    CHECK(pmm_core_free_pages(&state, first, 20));
    CHECK(pmm_core_free_pages(&state, second, 20));
    CHECK(pmm_core_alloc_pages(&state, 50, &combined));
    CHECK(combined == first); /* Free run crosses next-fit cursor at 42. */
    CHECK(pmm_core_free_pages(&state, combined, 50));

    CHECK(start_small(&state, &plan));
    for (uint64_t frame = 3; frame < 64; frame += 2u) {
        CHECK(pmm_core_reserve_range(&state, frame * MEMORY_PAGE_SIZE, 4096));
    }
    combined = UINT64_C(0x1111);
    CHECK(!pmm_core_alloc_pages(&state, 2, &combined)); /* Enough total, fragmented. */
    CHECK(combined == UINT64_C(0x1111));
    struct pmm_stats before;
    struct pmm_stats after;
    CHECK(pmm_core_get_stats(&state, &before));
    CHECK(pmm_core_alloc_pages(&state, 1, &first));
    CHECK(!pmm_core_free_pages(&state, first, 2)); /* Ends at reserved frame. */
    CHECK(pmm_core_get_stats(&state, &after));
    CHECK(after.free_frames + 1u == before.free_frames);
    CHECK(pmm_core_free_pages(&state, first, 1));
}

static void test_types_and_sparse_high_memory(void)
{
    struct memory_map map;
    memory_map_init(&map);
    add_region(&map, 0, 4u * MEMORY_PAGE_SIZE, UTAMO_MEMORY_RESERVED);
    add_region(&map, 4u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_ACPI_RECLAIMABLE);
    add_region(&map, 8u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_ACPI_NVS);
    add_region(&map, 12u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_BAD);
    add_region(&map, 16u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE);
    add_region(&map, 20u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_KERNEL_AND_MODULES);
    add_region(&map, 24u * MEMORY_PAGE_SIZE, 4u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_FRAMEBUFFER);
    add_region(&map, UINT64_C(0x100000), 256u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    add_region(&map, UINT64_C(0x100000000), 32u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    struct pmm_plan plan;
    CHECK(pmm_plan(&map, &plan));
    CHECK(plan.span_frames == UINT64_C(0x100020));
    CHECK(plan.bitmap_bytes == 131076 && plan.storage_bytes == 266240);
    CHECK(plan.storage_bytes <= sizeof(large_storage));
    struct pmm_state state = {0};
    CHECK(pmm_core_init(&state, &map, &plan, large_storage));
    CHECK(plan.storage_phys == UINT64_C(0x100000));
    for (uint64_t frame = 0; frame < 28; ++frame) {
        CHECK(!pmm_core_free_pages(&state, frame * MEMORY_PAGE_SIZE, 1));
    }
    CHECK(!pmm_core_free_pages(&state, UINT64_C(0x80000000), 1));
    struct pmm_stats stats;
    CHECK(pmm_core_get_stats(&state, &stats));
    CHECK(stats.total_frames == 288 && stats.free_frames == 223);
    uint64_t low;
    uint64_t high;
    CHECK(pmm_core_alloc_pages(&state, 191, &low));
    CHECK(low == UINT64_C(0x141000));
    CHECK(pmm_core_alloc_pages(&state, 32, &high));
    CHECK(high == UINT64_C(0x100000000)); /* No truncation above 4 GiB. */
    CHECK(!pmm_core_alloc_pages(&state, 1, &low));
    CHECK(pmm_core_free_pages(&state, high, 32));
    CHECK(pmm_core_free_pages(&state, low, 191));
    CHECK(pmm_core_get_stats(&state, &stats) && stats.free_frames == 223);
}

static void test_stress(void)
{
    struct memory_map map;
    memory_map_init(&map);
    add_region(&map, 0, 512u * MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    struct pmm_plan plan;
    CHECK(pmm_plan(&map, &plan));
    struct pmm_state state = {0};
    CHECK(pmm_core_init(&state, &map, &plan, small_storage));
    uint64_t pages[256];
    bool seen[512] = {false};
    struct pmm_stats before;
    struct pmm_stats after;
    CHECK(pmm_core_get_stats(&state, &before));
    for (unsigned int cycle = 0; cycle < 4; ++cycle) {
        (void)memset(seen, 0, sizeof(seen));
        for (size_t i = 0; i < 256; ++i) {
            CHECK(pmm_core_alloc_pages(&state, 1, &pages[i]));
            const uint64_t frame = pages[i] / MEMORY_PAGE_SIZE;
            CHECK(frame >= 2 && frame < 512 && memory_is_page_aligned(pages[i]));
            if (frame < 512) {
                CHECK(!seen[(size_t)frame]);
                seen[(size_t)frame] = true;
            }
        }
        CHECK(pmm_core_get_stats(&state, &after));
        CHECK(after.free_frames + 256u == before.free_frames);
        for (size_t i = 0; i < 256; i += 2u) {
            CHECK(pmm_core_free_pages(&state, pages[i], 1));
            CHECK(!pmm_core_free_pages(&state, pages[i], 1));
        }
        for (size_t i = 1; i < 256; i += 2u) {
            CHECK(pmm_core_free_pages(&state, pages[i], 1));
        }
        CHECK(pmm_core_get_stats(&state, &after));
        CHECK(after.free_frames == before.free_frames);
        CHECK(after.used_frames == before.used_frames);
    }
}

static void test_irq_wrappers(void)
{
    struct pmm_stats stats;
    CHECK(!pmm_get_stats(&stats));
    CHECK(simulated_flags == UINT64_C(0x202));
    struct memory_map map;
    small_map(&map);
    struct pmm_plan plan;
    CHECK(pmm_plan(&map, &plan));
    CHECK(pmm_init(&map, &plan, wrapper_storage));
    CHECK(!pmm_init(&map, &plan, wrapper_storage));
    uint64_t page;
    CHECK(pmm_alloc_page(&page));
    CHECK(simulated_flags == UINT64_C(0x202));
    CHECK(pmm_is_allocated_page(page));
    CHECK(!pmm_is_allocated_page(plan.storage_phys));
    CHECK(!pmm_reserve_range(page, 4096));
    CHECK(pmm_pin_page(page));
    CHECK(!pmm_is_allocated_page(page));
    CHECK(!pmm_free_page(page));
    CHECK(pmm_reserve_range(5u * MEMORY_PAGE_SIZE, 4096));
    simulated_flags = 2; /* Nested caller starts with IF clear. */
    CHECK(pmm_alloc_pages(2, &page));
    CHECK(simulated_flags == 2);
    CHECK(pmm_free_pages(page, 2));
    CHECK(simulated_flags == 2);
    CHECK(pmm_alloc_page(&page));
    CHECK(pmm_free_page(page));
    CHECK(!pmm_free_page(page));
    CHECK(pmm_get_stats(&stats));
    CHECK(stats.total_frames == 64 && stats.free_frames == 60);
    CHECK(stats.used_frames == 4);
    CHECK(critical_entries == critical_exits);
}

int main(void)
{
    test_plan();
    test_accounting_and_invalid_free();
    test_reservations_and_pinning();
    test_next_fit_and_atomicity();
    test_types_and_sparse_high_memory();
    test_stress();
    test_irq_wrappers();
    (void)printf("UTAMO PMM host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
