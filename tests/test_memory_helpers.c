/* SPDX-License-Identifier: MIT */
#include <utamo/hhdm.h>
#include <utamo/memory.h>
#include <utamo/vmm.h>
#include <limits.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
#define TEST_HHDM_OFFSET UINT64_C(0xffff800000000000)

static void add_region(struct memory_map *map, uint64_t base, uint64_t length,
                       enum memory_type type)
{
    const struct memory_region region = {base, length, type};
    CHECK(memory_map_add(map, &region));
}

static void test_alignment(void)
{
    static const struct {
        uint64_t value;
        uint64_t down;
        uint64_t up;
        bool up_valid;
    } cases[] = {
        {0, 0, 0, true},
        {1, 0, 4096, true},
        {4095, 0, 4096, true},
        {4096, 4096, 4096, true},
        {4097, 4096, 8192, true},
        {8191, 4096, 8192, true},
        {8192, 8192, 8192, true},
        {UINT64_C(0xffffffff), UINT64_C(0xfffff000),
         UINT64_C(0x100000000), true},
        {UINT64_C(0x100000000), UINT64_C(0x100000000),
         UINT64_C(0x100000000), true},
        {UINT64_C(0xffffffffffffefff), UINT64_C(0xffffffffffffe000),
         UINT64_C(0xfffffffffffff000), true},
        {UINT64_C(0xfffffffffffff000), UINT64_C(0xfffffffffffff000),
         UINT64_C(0xfffffffffffff000), true},
        {UINT64_C(0xfffffffffffff001), UINT64_C(0xfffffffffffff000), 0, false},
        {UINT64_MAX, UINT64_C(0xfffffffffffff000), 0, false}
    };
    CHECK(MEMORY_PAGE_SIZE == 4096);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint64_t output = 123;
        CHECK(memory_align_down(cases[i].value) == cases[i].down);
        CHECK(memory_is_page_aligned(cases[i].value) ==
              (cases[i].value == cases[i].down));
        CHECK(memory_align_up(cases[i].value, &output) == cases[i].up_valid);
        CHECK(output == (cases[i].up_valid ? cases[i].up : 123u));
    }
    CHECK(!memory_align_up(0, NULL));
    CHECK(!memory_align_up(UINT64_MAX, NULL));
}

static void test_canonical_indices_and_masks(void)
{
    static const struct {
        uint64_t value;
        bool canonical;
    } cases[] = {
        {0, true},
        {UINT64_C(0x00007fffffffffff), true},
        {UINT64_C(0x0000800000000000), false},
        {UINT64_C(0x0000ffffffffffff), false},
        {UINT64_C(0xffff000000000000), false},
        {UINT64_C(0xffff7fffffffffff), false},
        {UINT64_C(0xffff800000000000), true},
        {UINT64_MAX, true},
        {VMM_TEST_BASE, true}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(memory_is_canonical(cases[i].value) == cases[i].canonical);
    }
    static const struct {
        uint64_t value;
        unsigned int indices[4]; /* PT, PD, PDPT, PML4. */
    } decomposition[] = {
        {UINT64_C(0xffffab123456789a), {359, 418, 72, 342}},
        {UINT64_C(0x0000123456789abc), {393, 179, 209, 36}},
        {UINT64_C(0x00007fffffffffff), {511, 511, 511, 255}},
        {UINT64_C(0xffff800000000000), {0, 0, 0, 256}}
    };
    for (size_t i = 0; i < sizeof(decomposition) / sizeof(decomposition[0]); ++i) {
        uint64_t reconstructed = decomposition[i].value & UINT64_C(4095);
        for (unsigned int level = 1; level <= 4; ++level) {
            const unsigned int index = memory_page_index(decomposition[i].value, level);
            CHECK(index == decomposition[i].indices[level - 1u]);
            reconstructed |= (uint64_t)index << (12u + 9u * (level - 1u));
        }
        if ((reconstructed & (UINT64_C(1) << 47u)) != 0) {
            reconstructed |= UINT64_C(0xffff000000000000);
        }
        CHECK(reconstructed == decomposition[i].value);
    }
    CHECK(memory_page_index(0, 0) == 512);
    CHECK(memory_page_index(UINT64_MAX, 5) == 512);
    CHECK(memory_page_index(UINT64_MAX, UINT_MAX) == 512);
    for (unsigned int bits = 32; bits <= 52; ++bits) {
        uint64_t mask = 0;
        CHECK(memory_physical_mask(bits, &mask));
        CHECK(mask + MEMORY_PAGE_SIZE == (UINT64_C(1) << bits));
        CHECK(memory_is_page_aligned(mask));
    }
    uint64_t mask = UINT64_C(0x1234);
    CHECK(!memory_physical_mask(31, &mask));
    CHECK(mask == UINT64_C(0x1234));
    CHECK(!memory_physical_mask(53, &mask));
    CHECK(mask == UINT64_C(0x1234));
    CHECK(!memory_physical_mask(UINT_MAX, &mask));
    CHECK(mask == UINT64_C(0x1234));
    CHECK(!memory_physical_mask(48, NULL));
}

static void mapped_types_fixture(struct memory_map *map)
{
    memory_map_init(map);
    for (unsigned int type = 0; type <= (unsigned int)UTAMO_MEMORY_FRAMEBUFFER;
         ++type) {
        add_region(map, ((uint64_t)type + 1u) * UINT64_C(0x10000),
                   2u * MEMORY_PAGE_SIZE, (enum memory_type)type);
    }
    add_region(map, UINT64_C(0x100000000), 3u * MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    /* Unmapped MMIO does not need to fit MAXPHYADDR or HHDM addition. */
    add_region(map, UINT64_C(0xffffffffff000000), MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_RESERVED);
}

static void test_hhdm_types_and_roundtrip(void)
{
    struct memory_map map;
    mapped_types_fixture(&map);
    struct hhdm_context context = {0};
    CHECK(hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    CHECK(context.map == &map && context.offset == TEST_HHDM_OFFSET);
    CHECK(context.physical_mask == UINT64_C(0x0000fffffffff000));
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    for (unsigned int type = 0; type <= (unsigned int)UTAMO_MEMORY_FRAMEBUFFER;
         ++type) {
        const uint64_t phys = ((uint64_t)type + 1u) * UINT64_C(0x10000);
        const bool mapped = type == (unsigned int)UTAMO_MEMORY_USABLE ||
                            type == (unsigned int)UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE ||
                            type == (unsigned int)UTAMO_MEMORY_KERNEL_AND_MODULES ||
                            type == (unsigned int)UTAMO_MEMORY_FRAMEBUFFER;
        CHECK(hhdm_type_mapped((enum memory_type)type) == mapped);
        CHECK(hhdm_region(&context, phys, 8192) == &map.regions[type]);
        uint64_t virt = 123;
        uint64_t reverse = 456;
        CHECK(hhdm_translate(&context, phys + 31u, 4096, &virt) == mapped);
        CHECK(virt == (mapped ? TEST_HHDM_OFFSET + phys + 31u : 123u));
        CHECK(hhdm_reverse(&context, TEST_HHDM_OFFSET + phys + 31u, 4096,
                            &reverse) == mapped);
        CHECK(reverse == (mapped ? phys + 31u : 456u));
    }
    CHECK(!hhdm_type_mapped((enum memory_type)8));
    uint64_t virt;
    uint64_t reverse;
    CHECK(hhdm_translate(&context, UINT64_C(0x100000000), 12288, &virt));
    CHECK(virt == UINT64_C(0xffff800100000000));
    CHECK(hhdm_reverse(&context, virt, 12288, &reverse));
    CHECK(reverse == UINT64_C(0x100000000));
    CHECK(hhdm_translate(&context, UINT64_C(0x100002fff), 1, &virt));
    CHECK(virt == UINT64_C(0xffff800100002fff));
    CHECK(!hhdm_translate(&context, UINT64_C(0x100002fff), 2, &virt));
    CHECK(!hhdm_translate(&context, UINT64_C(0x100003000), 1, &virt));
    CHECK(!hhdm_translate(&context, 0, 1, &virt));
    CHECK(!hhdm_translate(&context, UINT64_C(0x200000), 1, &virt));
    CHECK(!hhdm_translate(&context, UINT64_C(0xffffffffff000000), 1, &virt));
}

static void test_hhdm_failure_preservation(void)
{
    struct memory_map map;
    mapped_types_fixture(&map);
    struct hhdm_context context = {0};
    uint64_t output = UINT64_C(0xabcdef);
    CHECK(!hhdm_translate(&context, 0, 1, &output));
    CHECK(!hhdm_reverse(&context, TEST_HHDM_OFFSET, 1, &output));
    CHECK(hhdm_region(&context, 0, 1) == NULL);
    CHECK(hhdm_region(NULL, 0, 1) == NULL);
    CHECK(!hhdm_init(NULL, &map, TEST_HHDM_OFFSET, 48));
    CHECK(!hhdm_init(&context, NULL, TEST_HHDM_OFFSET, 48));
    CHECK(!hhdm_init(&context, &map, 0, 48));
    CHECK(!hhdm_init(&context, &map, UINT64_C(0xffff7ffffffff000), 48));
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET + 1u, 48));
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 31));
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 53));
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 32)); /* >4-GiB usable. */
    CHECK(!hhdm_init(&context, &map, UINT64_C(0xfffffffffffff000), 48));
    CHECK(!context.initialized && context.map == NULL && context.offset == 0);
    CHECK(hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    CHECK(!hhdm_translate(NULL, 0, 1, &output));
    CHECK(!hhdm_reverse(NULL, TEST_HHDM_OFFSET, 1, &output));
    CHECK(!hhdm_translate(&context, UINT64_C(0x10000), 0, &output));
    CHECK(!hhdm_translate(&context, UINT64_C(0x10000), 1, NULL));
    CHECK(!hhdm_translate(&context, UINT64_MAX, 2, &output));
    CHECK(!hhdm_translate(&context, UINT64_C(0x10000), SIZE_MAX, &output));
    CHECK(!hhdm_reverse(&context, TEST_HHDM_OFFSET - 1u, 1, &output));
    CHECK(!hhdm_reverse(&context, TEST_HHDM_OFFSET + UINT64_C(0x10000), 0, &output));
    CHECK(!hhdm_reverse(&context, TEST_HHDM_OFFSET + UINT64_C(0x10000), 1, NULL));
    CHECK(!hhdm_reverse(&context, UINT64_MAX, 2, &output));
    CHECK(output == UINT64_C(0xabcdef));
    CHECK(hhdm_region(&context, UINT64_MAX, 2) == NULL);
}

static void test_hhdm_boundaries_and_invalid_maps(void)
{
    struct memory_map map;
    memory_map_init(&map);
    add_region(&map, MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    add_region(&map, 2u * MEMORY_PAGE_SIZE, MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    add_region(&map, 4u * MEMORY_PAGE_SIZE + 3u, MEMORY_PAGE_SIZE + 1u,
               UTAMO_MEMORY_FRAMEBUFFER);
    struct hhdm_context context = {0};
    CHECK(hhdm_init(&context, &map, TEST_HHDM_OFFSET, 32));
    uint64_t virt;
    uint64_t phys;
    CHECK(hhdm_translate(&context, 4096, 4096, &virt));
    CHECK(!hhdm_translate(&context, 4096, 4097, &virt));
    CHECK(hhdm_region(&context, 4096, 4097) == NULL);
    CHECK(!hhdm_reverse(&context, TEST_HHDM_OFFSET + 8191u, 2, &phys));
    CHECK(hhdm_translate(&context, 4u * MEMORY_PAGE_SIZE + 3u, 4097, &virt));
    CHECK(!hhdm_translate(&context, 4u * MEMORY_PAGE_SIZE + 2u, 1, &virt));

    context = (struct hhdm_context){0};
    memory_map_init(&map);
    add_region(&map, UINT64_C(0xfffff000), MEMORY_PAGE_SIZE, UTAMO_MEMORY_USABLE);
    CHECK(hhdm_init(&context, &map, TEST_HHDM_OFFSET, 32));
    CHECK(hhdm_translate(&context, UINT64_C(0xffffffff), 1, &virt));
    CHECK(hhdm_reverse(&context, virt, 1, &phys) && phys == UINT64_C(0xffffffff));

    context = (struct hhdm_context){.offset = 123};
    memory_map_init(&map);
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    CHECK(context.offset == 123 && !context.initialized);
    map.count = UTAMO_MEMORY_REGION_LIMIT + 1u;
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    map.count = 1;
    map.regions[0] = (struct memory_region){UINT64_MAX - 1u, 4,
                                          UTAMO_MEMORY_USABLE};
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    map.regions[0] = (struct memory_region){MEMORY_PAGE_SIZE, 0,
                                          UTAMO_MEMORY_USABLE};
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    map.regions[0] = (struct memory_region){MEMORY_PAGE_SIZE + 1u, 4096,
                                          UTAMO_MEMORY_USABLE};
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    map.regions[0] = (struct memory_region){MEMORY_PAGE_SIZE, 4096,
                                          (enum memory_type)8};
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    map.regions[0] = (struct memory_region){MEMORY_PAGE_SIZE, 4096,
                                          UTAMO_MEMORY_USABLE};
    map.count = 2;
    map.regions[1] = (struct memory_region){0, 4096, UTAMO_MEMORY_RESERVED};
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 48));
    CHECK(context.offset == 123 && !context.initialized);

    memory_map_init(&map);
    add_region(&map, UINT64_C(0x0000800000000000), MEMORY_PAGE_SIZE,
               UTAMO_MEMORY_USABLE);
    CHECK(!hhdm_init(&context, &map, TEST_HHDM_OFFSET, 52)); /* HHDM sum wraps. */
}

int main(void)
{
    test_alignment();
    test_canonical_indices_and_masks();
    test_hhdm_types_and_roundtrip();
    test_hhdm_failure_preservation();
    test_hhdm_boundaries_and_invalid_maps();
    (void)printf("UTAMO memory helper host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0 ? 0 : 1;
}
