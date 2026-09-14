/* SPDX-License-Identifier: MIT */
/* Host-only tests. This file is never linked into utamo-kernel. */
#include <utamo/format.h>
#include <utamo/memory_map.h>
#include <utamo/string.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static unsigned int failures;
static unsigned int checks;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

/* Independent oracle: do not use the functions under test to judge results. */
static bool bytes_equal(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    for (size_t index = 0; index < count; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static void test_memory(void)
{
    unsigned char destination[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    const unsigned char source[] = {0x80, 0x00, 0xff};
    const unsigned char copied[] = {0x11, 0x80, 0x00, 0xff, 0x55};
    CHECK(memcpy(destination + 1, source, sizeof(source)) == destination + 1);
    CHECK(bytes_equal(destination, copied, sizeof(copied)));
    CHECK(memcpy(destination, destination, sizeof(destination)) == destination);
    CHECK(bytes_equal(destination, copied, sizeof(copied)));
    CHECK(memcpy(NULL, NULL, 0) == NULL);

    CHECK(memset(destination + 1, 0x1ab, 3) == destination + 1);
    const unsigned char filled[] = {0x11, 0xab, 0xab, 0xab, 0x55};
    CHECK(bytes_equal(destination, filled, sizeof(filled)));
    CHECK(memset(NULL, 0, 0) == NULL);

    char right_overlap[] = "abcdef";
    CHECK(memmove(right_overlap + 1, right_overlap, 5) == right_overlap + 1);
    CHECK(bytes_equal(right_overlap, "aabcde", sizeof(right_overlap)));

    char left_overlap[] = "abcdef";
    CHECK(memmove(left_overlap, left_overlap + 1, 5) == left_overlap);
    CHECK(bytes_equal(left_overlap, "bcdeff", sizeof(left_overlap)));
    CHECK(memmove(left_overlap, left_overlap, sizeof(left_overlap)) ==
          left_overlap);
    CHECK(bytes_equal(left_overlap, "bcdeff", sizeof(left_overlap)));
    CHECK(memmove(NULL, NULL, 0) == NULL);

    unsigned char nonoverlap[3] = {1, 2, 3};
    CHECK(memmove(nonoverlap, source, sizeof(source)) == nonoverlap);
    CHECK(bytes_equal(nonoverlap, source, sizeof(source)));

    const unsigned char low[] = {0, 0x7f};
    const unsigned char high[] = {0, 0x80};
    CHECK(memcmp(low, high, sizeof(low)) < 0);
    CHECK(memcmp(high, low, sizeof(low)) > 0);
    CHECK(memcmp(low, high, 1) == 0);
    CHECK(memcmp(high, high, sizeof(high)) == 0);
    CHECK(memcmp(NULL, NULL, 0) == 0);
}

static void test_strings(void)
{
    CHECK(strlen("") == 0);
    CHECK(strlen("utamo") == 5);
    CHECK(strlen("a\0ignored") == 1);
    CHECK(strcmp("same", "same") == 0);
    CHECK(strcmp("a", "aa") < 0);
    CHECK(strcmp("ab", "aa") > 0);
    CHECK(strcmp("", "") == 0);
    const char high[] = {(char)0x80, '\0'};
    const char low[] = {(char)0x7f, '\0'};
    CHECK(strcmp(high, low) > 0);
    CHECK(strncmp(high, low, 1) > 0);
    CHECK(strncmp("abcX", "abcY", 3) == 0);
    CHECK(strncmp("abcX", "abcY", 4) < 0);
    CHECK(strncmp("a", "aa", 2) < 0);
    CHECK(strncmp("", "", 128) == 0);
    CHECK(strncmp(NULL, NULL, 0) == 0);
    /* No NUL is needed inside a fully readable counted range. */
    const char counted_left[] = {'a', 'b'};
    const char counted_right[] = {'a', 'b'};
    CHECK(strncmp(counted_left, counted_right, 2) == 0);
}

static void test_format_values(void)
{
    char output[256];
    const char *word = "utamo";
    const size_t length = ksnprintf(output, sizeof(output),
                                   "%s %c %d %u %x %%", word, '>', -42, 42U,
                                   0xdeadU);
    static const char expected[] = "utamo > -42 42 dead %";
    CHECK(length == sizeof(expected) - 1);
    CHECK(bytes_equal(output, expected, sizeof(expected)));

    char oracle[256];
    const int oracle_length = snprintf(oracle, sizeof(oracle),
        "%d %d %u %x %lld %lld %llu %llx", INT_MIN, INT_MAX, UINT_MAX,
        UINT_MAX, LLONG_MIN, LLONG_MAX, ULLONG_MAX, ULLONG_MAX);
    CHECK(oracle_length > 0 && (size_t)oracle_length < sizeof(oracle));
    const size_t boundary_length = ksnprintf(output, sizeof(output),
        "%d %d %u %x %lld %lld %llu %llx", INT_MIN, INT_MAX, UINT_MAX,
        UINT_MAX, LLONG_MIN, LLONG_MAX, ULLONG_MAX, ULLONG_MAX);
    CHECK(boundary_length == (size_t)oracle_length);
    if (oracle_length >= 0 && (size_t)oracle_length < sizeof(oracle)) {
        CHECK(bytes_equal(output, oracle, (size_t)oracle_length + 1));
    }

    static const char zeroes[] = "0 0 0 0 0 0 0x0";
    CHECK(ksnprintf(output, sizeof(output), "%d %u %x %lld %llu %llx %p",
                    0, 0U, 0U, 0LL, 0ULL, 0ULL, (void *)NULL) ==
          sizeof(zeroes) - 1);
    CHECK(bytes_equal(output, zeroes, sizeof(zeroes)));
    /* Integer-to-pointer construction is supported by our flat x86 hosts. */
    CHECK(ksnprintf(output, sizeof(output), "%p", (void *)(uintptr_t)0x1234) ==
          6);
    CHECK(bytes_equal(output, "0x1234", 7));

    CHECK(ksnprintf(output, sizeof(output), "%s", (const char *)NULL) == 6);
    CHECK(bytes_equal(output, "(null)", 7));
    CHECK(ksnprintf(output, sizeof(output), NULL) == 6);
    CHECK(bytes_equal(output, "(null)", 7));
}

static void test_format_bounds(void)
{
    char guarded[] = {'L', 'a', 'b', 'c', 'd', 'R'};
    CHECK(ksnprintf(guarded + 1, 4, "%s", (const char *)"abcdef") == 6);
    const char expected[] = {'L', 'a', 'b', 'c', '\0', 'R'};
    CHECK(bytes_equal(guarded, expected, sizeof(guarded)));

    char single[] = {'L', 'x', 'R'};
    CHECK(ksnprintf(single + 1, 1, "abcdef") == 6);
    const char single_expected[] = {'L', '\0', 'R'};
    CHECK(bytes_equal(single, single_expected, sizeof(single)));
    CHECK(ksnprintf(single + 1, 0, "abcdef") == 6);
    CHECK(bytes_equal(single, single_expected, sizeof(single)));
    CHECK(ksnprintf(NULL, 0, "abc%d", 42) == 5);
    CHECK(ksnprintf(NULL, 100, "abc%d", 42) == 5);

    char exact[4];
    CHECK(ksnprintf(exact, sizeof(exact), "abc") == 3);
    CHECK(bytes_equal(exact, "abc", sizeof(exact)));
    CHECK(ksnprintf(exact, sizeof(exact), "") == 0);
    CHECK(exact[0] == '\0');

    /* Embedded NUL is still an emitted character and counts toward length. */
    char binary[4];
    CHECK(ksnprintf(binary, sizeof(binary), "A%cB", 0) == 3);
    const char binary_expected[] = {'A', '\0', 'B', '\0'};
    CHECK(bytes_equal(binary, binary_expected, sizeof(binary)));
}

static void test_format_invalid(void)
{
    char output[128];
    static const char expected[] = "%n/%q/%llq/%ld/%lls/77";
    CHECK(ksnprintf(output, sizeof(output), "%n/%q/%llq/%ld/%lls/%d", 77) ==
          sizeof(expected) - 1);
    CHECK(bytes_equal(output, expected, sizeof(expected)));
    CHECK(ksnprintf(output, sizeof(output), "tail%") == 5);
    CHECK(bytes_equal(output, "tail%", 6));
    CHECK(ksnprintf(output, sizeof(output), "tail%l") == 6);
    CHECK(bytes_equal(output, "tail%l", 7));
    CHECK(ksnprintf(output, sizeof(output), "tail%ll") == 7);
    CHECK(bytes_equal(output, "tail%ll", 8));
    CHECK(ksnprintf(output, sizeof(output), "%ll%%d", 9) == 5);
    CHECK(bytes_equal(output, "%ll%9", 6));
    CHECK(ksnprintf(output, sizeof(output), "%08x") == 4);
    CHECK(bytes_equal(output, "%08x", 5));
    kformat(NULL, NULL, "%d", 1);
}

struct capture {
    char bytes[32];
    size_t length;
};

static void capture_character(char ch, void *context)
{
    struct capture *capture = context;
    if (capture->length < sizeof(capture->bytes)) {
        capture->bytes[capture->length] = ch;
    }
    ++capture->length;
}

static void check_va_list_preserved(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char first[32];
    char second[32];
    const size_t first_length = kvsnprintf(first, sizeof(first), format, args);
    const size_t second_length = kvsnprintf(second, sizeof(second), format,
                                           args);
    struct capture capture = {.bytes = {0}, .length = 0};
    kvformat(capture_character, &capture, format, args);
    va_end(args);
    CHECK(first_length == 5);
    CHECK(second_length == first_length);
    CHECK(bytes_equal(first, "12/34", 6));
    CHECK(bytes_equal(first, second, 6));
    CHECK(capture.length == 5);
    CHECK(bytes_equal(capture.bytes, "12/34", 5));
}

static void test_format_callbacks(void)
{
    struct capture capture = {.bytes = {0}, .length = 0};
    kformat(capture_character, &capture, "%c%s", 'A', (const char *)"BC");
    CHECK(capture.length == 3);
    CHECK(bytes_equal(capture.bytes, "ABC", 3));
    check_va_list_preserved("%d/%u", 12, 34U);
}

static void test_memory_map_basic(void)
{
    struct memory_map map = {0};
    memory_map_init(&map);
    memory_map_init(NULL);
    CHECK(map.count == 0);
    CHECK(map.usable_bytes == 0);
    CHECK(map.bootloader_reclaimable_bytes == 0);
    const struct memory_region regions[] = {
        {0, 0x1000, UTAMO_MEMORY_RESERVED},
        {0x1000, 0x2000, UTAMO_MEMORY_USABLE},
        {0x3000, 0x1000, UTAMO_MEMORY_USABLE},
        {0x4000, 0x1000, UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE},
        {0x5000, 0x1000, UTAMO_MEMORY_ACPI_RECLAIMABLE},
        {0x6000, 0x1000, UTAMO_MEMORY_ACPI_NVS},
        {0x7000, 0x1000, UTAMO_MEMORY_BAD},
        {0x8000, 0x1000, UTAMO_MEMORY_KERNEL_AND_MODULES},
        {0x9000, 0x1000, UTAMO_MEMORY_FRAMEBUFFER}
    };
    for (size_t index = 0; index < sizeof(regions) / sizeof(regions[0]);
         ++index) {
        CHECK(memory_map_add(&map, &regions[index]));
        CHECK(map.regions[index].base == regions[index].base);
        CHECK(map.regions[index].length == regions[index].length);
        CHECK(map.regions[index].type == regions[index].type);
    }
    CHECK(map.count == sizeof(regions) / sizeof(regions[0]));
    CHECK(map.usable_bytes == 0x3000);
    CHECK(map.bootloader_reclaimable_bytes == 0x1000);
    CHECK(!memory_map_add(NULL, &regions[0]));
    CHECK(!memory_map_add(&map, NULL));
    CHECK(map.count == sizeof(regions) / sizeof(regions[0]));

    memory_map_init(&map);
    CHECK(map.count == 0);
    CHECK(map.usable_bytes == 0);
    CHECK(map.bootloader_reclaimable_bytes == 0);
    CHECK(memory_map_add(&map, &regions[1]));
    CHECK(map.usable_bytes == 0x2000);
}

static void test_memory_map_rejections(void)
{
    struct memory_map map = {0};
    memory_map_init(&map);
    const struct memory_region first = {0x2000, 0x2000, UTAMO_MEMORY_USABLE};
    CHECK(memory_map_add(&map, &first));
    const struct memory_region rejected[] = {
        /* Ordering, zero length, overflow, unknown types, and alignment. */
        {0x1000, 0x1000, UTAMO_MEMORY_RESERVED},
        {0x4000, 0, UTAMO_MEMORY_RESERVED},
        {UINT64_MAX, 1, UTAMO_MEMORY_RESERVED},
        {UINT64_MAX - 7, 8, UTAMO_MEMORY_RESERVED},
        {0x4000, 0x1000, (enum memory_type)-1},
        {0x4000, 0x1000, (enum memory_type)999},
        {0x4001, 0x1000, UTAMO_MEMORY_USABLE},
        {0x4000, 0x1001, UTAMO_MEMORY_USABLE},
        {0x4001, 0x1000, UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE},
        {0x4000, 1, UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE},
        /* Equal bases and partial overlap with exclusive memory. */
        {0x2000, 0x1000, UTAMO_MEMORY_USABLE},
        {0x3000, 0x2000, UTAMO_MEMORY_RESERVED},
        {0x3000, 0x2000, UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE}
    };
    for (size_t index = 0; index < sizeof(rejected) / sizeof(rejected[0]);
         ++index) {
        CHECK(!memory_map_add(&map, &rejected[index]));
        /* Failed insertions must leave all observable active state intact. */
        CHECK(map.count == 1);
        CHECK(map.usable_bytes == 0x2000);
        CHECK(map.bootloader_reclaimable_bytes == 0);
        CHECK(map.regions[0].base == first.base);
        CHECK(map.regions[0].length == first.length);
        CHECK(map.regions[0].type == first.type);
    }
    const struct memory_region adjacent = {0x4000, 0x1000,
                                          UTAMO_MEMORY_USABLE};
    CHECK(memory_map_add(&map, &adjacent));
    CHECK(map.usable_bytes == 0x3000);
}

static void test_memory_map_overlap(void)
{
    struct memory_map map = {0};
    memory_map_init(&map);
    const struct memory_region covering = {1, 0x8fff, UTAMO_MEMORY_RESERVED};
    const struct memory_region same_base = {1, 1, UTAMO_MEMORY_ACPI_NVS};
    const struct memory_region nested = {0x1000, 0x1000,
                                        UTAMO_MEMORY_FRAMEBUFFER};
    const struct memory_region exclusive = {0x3000, 0x1000,
                                           UTAMO_MEMORY_USABLE};
    CHECK(memory_map_add(&map, &covering));
    CHECK(memory_map_add(&map, &same_base));
    CHECK(memory_map_add(&map, &nested));
    /* The conflict is with the first region, not the immediate predecessor. */
    CHECK(!memory_map_add(&map, &exclusive));
    CHECK(map.count == 3);
    CHECK(map.usable_bytes == 0);
    CHECK(map.bootloader_reclaimable_bytes == 0);

    const struct memory_region reclaimable = {0x3000, 0x1000,
        UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE};
    CHECK(!memory_map_add(&map, &reclaimable));
    const struct memory_region after_covering = {0x9000, 0x1000,
        UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE};
    CHECK(memory_map_add(&map, &after_covering));
    const struct memory_region overlap_reclaimable = {0x9001, 1,
                                                     UTAMO_MEMORY_RESERVED};
    CHECK(!memory_map_add(&map, &overlap_reclaimable));
    CHECK(map.count == 4);
    CHECK(map.bootloader_reclaimable_bytes == 0x1000);
}

static void test_memory_map_limits(void)
{
    struct memory_map map = {0};
    memory_map_init(&map);
    /* Largest valid aligned usable extent under the exclusive-end contract. */
    const uint64_t largest_aligned = UINT64_MAX & ~UINT64_C(0xfff);
    const struct memory_region enormous = {0, largest_aligned,
                                           UTAMO_MEMORY_USABLE};
    CHECK(memory_map_add(&map, &enormous));
    CHECK(map.usable_bytes == largest_aligned);
    const struct memory_region overflowing = {largest_aligned, 0x1000,
                                              UTAMO_MEMORY_USABLE};
    CHECK(!memory_map_add(&map, &overflowing));
    CHECK(map.usable_bytes == largest_aligned);
    CHECK(map.count == 1);
    /* End == UINT64_MAX is representable; end == 2^64 is rejected above. */
    const struct memory_region tail = {largest_aligned, 0xfff,
                                       UTAMO_MEMORY_RESERVED};
    CHECK(memory_map_add(&map, &tail));
    CHECK(map.usable_bytes == largest_aligned);

    memory_map_init(&map);
    const struct memory_region huge_reclaimable = {0, largest_aligned,
        UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE};
    CHECK(memory_map_add(&map, &huge_reclaimable));
    CHECK(map.bootloader_reclaimable_bytes == largest_aligned);
    CHECK(map.usable_bytes == 0);

    memory_map_init(&map);
    CHECK(UTAMO_MEMORY_REGION_LIMIT == 512U);
    for (size_t index = 0; index < UTAMO_MEMORY_REGION_LIMIT; ++index) {
        const struct memory_region region = {(uint64_t)index * 0x1000,
                                             0x1000, UTAMO_MEMORY_USABLE};
        CHECK(memory_map_add(&map, &region));
    }
    CHECK(map.count == UTAMO_MEMORY_REGION_LIMIT);
    CHECK(map.usable_bytes == (uint64_t)UTAMO_MEMORY_REGION_LIMIT * 0x1000);
    const struct memory_region excess = {
        (uint64_t)UTAMO_MEMORY_REGION_LIMIT * 0x1000, 0x1000,
        UTAMO_MEMORY_USABLE
    };
    CHECK(!memory_map_add(&map, &excess));
    CHECK(map.count == UTAMO_MEMORY_REGION_LIMIT);
    CHECK(map.usable_bytes == (uint64_t)UTAMO_MEMORY_REGION_LIMIT * 0x1000);
}

int main(void)
{
    test_memory();
    test_strings();
    test_format_values();
    test_format_bounds();
    test_format_invalid();
    test_format_callbacks();
    test_memory_map_basic();
    test_memory_map_rejections();
    test_memory_map_overlap();
    test_memory_map_limits();
    (void)printf("UTAMO host tests: %u checks, %u failures\n", checks,
                 failures);
    return failures == 0 ? 0 : 1;
}
