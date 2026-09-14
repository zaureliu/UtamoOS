/* SPDX-License-Identifier: MIT */
#include <utamo/heap_core.h>
#include <utamo/string.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

_Alignas(16) static unsigned char arena[65536];
struct growth_model {
    size_t mapped;
    size_t limit;
    unsigned int calls;
    bool fail;
};
static struct growth_model growth;

static bool grow(void *context, size_t old_mapped, size_t new_mapped)
{
    struct growth_model *model = context;
    ++model->calls;
    CHECK(old_mapped == model->mapped);
    CHECK(new_mapped > old_mapped && new_mapped <= model->limit);
    if (model->fail) {
        return false;
    }
    (void)memset(arena + old_mapped, 0x7e, new_mapped - old_mapped);
    model->mapped = new_mapped;
    return true;
}

static void start(struct heap_core *heap, size_t initial, size_t maximum,
                  size_t quantum, bool poison, bool expandable)
{
    *heap = (struct heap_core){0};
    growth = (struct growth_model){.mapped = initial, .limit = maximum};
    const struct heap_ops ops = {.context = &growth, .grow = grow};
    CHECK(heap_core_init(heap, arena, initial, maximum, quantum, poison,
                         expandable ? &ops : NULL));
    CHECK(heap_core_validate(heap));
}

static struct heap_stats stats(const struct heap_core *heap)
{
    struct heap_stats value = {0};
    CHECK(heap_core_get_stats(heap, &value));
    CHECK(value.mapped_bytes == value.used_bytes + value.free_bytes +
          value.overhead_bytes);
    return value;
}

static bool filled(const unsigned char *bytes, size_t size, unsigned char value)
{
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

static void test_init_and_zero(void)
{
    struct heap_core heap = {0};
    (void)memset(arena, 0x55, sizeof(arena));
    CHECK(!heap_core_init(NULL, arena, 64, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, NULL, 64, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena + 1, 64, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 48, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 128, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 65, 128, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 64, 129, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 64, 128, 63, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 64, SIZE_MAX - 15u, 64, false, NULL));
    CHECK(!heap.initialized && arena[0] == 0x55);
    CHECK(!heap_core_validate(&heap));
    CHECK(heap_core_alloc(&heap, 1) == NULL);
    CHECK(heap_core_free(NULL, NULL));
    CHECK(heap_core_free(&heap, NULL));
    CHECK(heap_core_realloc(&heap, NULL, 0) == NULL);
    CHECK(heap_core_init(&heap, arena, 64, 64, 64, false, NULL));
    CHECK(!heap_core_init(&heap, arena, 64, 64, 64, false, NULL));
    struct heap_stats before = stats(&heap);
    CHECK(before.mapped_bytes == 64 && before.free_bytes == 16);
    CHECK(before.used_bytes == 0 && before.overhead_bytes == 48);
    CHECK(heap_core_alloc(&heap, 0) == NULL);
    CHECK(heap_core_calloc(&heap, 0, SIZE_MAX) == NULL);
    CHECK(heap_core_calloc(&heap, SIZE_MAX, 0) == NULL);
    CHECK(heap_core_free(&heap, NULL));
    struct heap_stats after = stats(&heap);
    CHECK(after.allocations == 0 && after.frees == 0);
    CHECK(after.failed_allocations == before.failed_allocations);
    CHECK(!heap_core_get_stats(&heap, NULL));
    struct heap_stats unchanged = {.mapped_bytes = 123};
    CHECK(!heap_core_get_stats(NULL, &unchanged));
    CHECK(unchanged.mapped_bytes == 123);
}

static void test_alignment_split_and_oom(void)
{
    struct heap_core heap;
    start(&heap, 4096, 4096, 64, true, false);
    static const size_t sizes[] = {1, 15, 16, 17, 31, 32, 33, 127};
    void *pointers[sizeof(sizes) / sizeof(sizes[0])] = {0};
    uint64_t used = 0;
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        pointers[i] = heap_core_alloc(&heap, sizes[i]);
        CHECK(pointers[i] != NULL);
        CHECK(((uintptr_t)pointers[i] & 15u) == 0);
        CHECK(filled(pointers[i], sizes[i], 0xcd));
        (void)memset(pointers[i], (int)i + 1, sizes[i]);
        used += (uint64_t)sizes[i];
        CHECK(heap_core_validate(&heap));
    }
    struct heap_stats current = stats(&heap);
    CHECK(current.used_bytes == used && current.live_allocations == 8);
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        CHECK(filled(pointers[i], sizes[i], (unsigned char)(i + 1u)));
        CHECK(heap_core_free(&heap, pointers[i]));
        CHECK(heap_core_validate(&heap));
    }
    current = stats(&heap);
    CHECK(current.live_allocations == 0 && current.used_bytes == 0);
    CHECK(current.free_bytes == 4096u - 48u && current.overhead_bytes == 48);
    CHECK(current.allocations == 8 && current.frees == 8 && current.peak_usage == used);

    start(&heap, 128, 128, 64, false, false);
    void *first = heap_core_alloc(&heap, 16); /* 64-byte block plus 64-byte split. */
    CHECK(first != NULL);
    current = stats(&heap);
    CHECK(current.used_bytes == 16 && current.free_bytes == 16);
    CHECK(current.overhead_bytes == 96);
    void *second = heap_core_alloc(&heap, 16);
    CHECK(second != NULL && second != first);
    CHECK(heap_core_alloc(&heap, 1) == NULL);
    current = stats(&heap);
    CHECK(current.used_bytes == 32 && current.free_bytes == 0);
    CHECK(current.failed_allocations == 1);
    CHECK(heap_core_free(&heap, first));
    CHECK(heap_core_free(&heap, second));
    CHECK(heap_core_validate(&heap));

    start(&heap, 128, 128, 64, false, false);
    first = heap_core_alloc(&heap, 17); /* Remainder48 cannot form another block. */
    CHECK(first != NULL && heap_core_alloc(&heap, 1) == NULL);
    current = stats(&heap);
    CHECK(current.used_bytes == 17 && current.free_bytes == 0);
    CHECK(current.overhead_bytes == 111);
    CHECK(heap_core_free(&heap, first));
    CHECK(heap_core_validate(&heap));
    CHECK(heap_core_alloc(&heap, SIZE_MAX) == NULL);
    CHECK(heap_core_calloc(&heap, SIZE_MAX / 2u + 1u, 2) == NULL);
    current = stats(&heap);
    CHECK(current.failed_allocations == 3);
}

static void test_coalescing_and_invalid_frees(void)
{
    struct heap_core heap;
    start(&heap, 256, 256, 64, true, false);
    void *a = heap_core_alloc(&heap, 16);
    void *b = heap_core_alloc(&heap, 16);
    void *c = heap_core_alloc(&heap, 16);
    void *d = heap_core_alloc(&heap, 16);
    CHECK(a != NULL && b != NULL && c != NULL && d != NULL);
    CHECK(heap_core_free(&heap, b));
    CHECK(heap_core_free(&heap, c)); /* Backward coalesce. */
    struct heap_stats current = stats(&heap);
    CHECK(current.largest_free_bytes == 80 && current.free_bytes == 80);
    CHECK(heap_core_alloc(&heap, 96) == NULL); /* Fragmentation/insufficient run. */
    CHECK(heap_core_free(&heap, a)); /* Forward coalesce. */
    current = stats(&heap);
    CHECK(current.largest_free_bytes == 144);
    CHECK(heap_core_free(&heap, d)); /* Coalesce previous and arena tail. */
    CHECK(heap_core_validate(&heap));
    current = stats(&heap);
    CHECK(current.free_bytes == 208 && current.overhead_bytes == 48);
    CHECK(filled(arena + 48, 208, 0xdd));
    CHECK(!heap_core_free(&heap, b)); /* Header was absorbed and poisoned. */
    CHECK(!heap_core_free(&heap, a)); /* Still a header, but already free. */
    CHECK(!heap_core_free(&heap, arena));
    CHECK(!heap_core_free(&heap, arena + 256));
    CHECK(!heap_core_free(&heap, arena + 512));
    CHECK(!heap_core_free(&heap, (void *)((uintptr_t)arena - 16u)));
    a = heap_core_alloc(&heap, 64);
    CHECK(a != NULL);
    CHECK(!heap_core_free(&heap, (unsigned char *)a + 1));
    CHECK(!heap_core_free(&heap, (unsigned char *)a + 16));
    CHECK(!heap_core_free(&heap, (unsigned char *)a - 16));
    current = stats(&heap);
    CHECK(current.invalid_frees == 9 && current.live_allocations == 1);
    CHECK(heap_core_free(&heap, a));

    start(&heap, 512, 512, 64, false, false);
    a = heap_core_alloc(&heap, 32);
    b = heap_core_alloc(&heap, 32);
    c = heap_core_alloc(&heap, 32);
    CHECK(heap_core_free(&heap, a));
    CHECK(heap_core_free(&heap, c));
    CHECK(heap_core_free(&heap, b)); /* Merge a free block on both sides. */
    current = stats(&heap);
    CHECK(current.free_bytes == 464 && current.overhead_bytes == 48);
}

static void test_fragmentation(void)
{
    struct heap_core heap;
    start(&heap, 512, 512, 64, false, false);
    void *pointers[8];
    for (size_t i = 0; i < 8; ++i) {
        pointers[i] = heap_core_alloc(&heap, 16);
        CHECK(pointers[i] != NULL);
    }
    for (size_t i = 0; i < 8; i += 2u) {
        CHECK(heap_core_free(&heap, pointers[i]));
    }
    struct heap_stats current = stats(&heap);
    CHECK(current.free_bytes == 64 && current.largest_free_bytes == 16);
    CHECK(heap_core_alloc(&heap, 32) == NULL);
    CHECK(heap_core_validate(&heap));
    for (size_t i = 1; i < 8; i += 2u) {
        CHECK(heap_core_free(&heap, pointers[i]));
    }
    current = stats(&heap);
    CHECK(current.free_bytes == 464 && current.failed_allocations == 1);
}

static void test_growth_and_rollback(void)
{
    struct heap_core heap;
    start(&heap, 128, 1024, 128, true, true);
    void *a = heap_core_alloc(&heap, 80);
    CHECK(a != NULL);
    (void)memset(a, 0x42, 80);
    growth.fail = true;
    const struct heap_stats before = stats(&heap);
    void *b = heap_core_alloc(&heap, 200);
    CHECK(b == NULL && growth.calls == 1 && growth.mapped == 128);
    struct heap_stats current = stats(&heap);
    CHECK(current.mapped_bytes == before.mapped_bytes &&
          current.used_bytes == before.used_bytes &&
          current.free_bytes == before.free_bytes);
    CHECK(current.failed_allocations == before.failed_allocations + 1u);
    CHECK(filled(a, 80, 0x42) && heap_core_validate(&heap));
    growth.fail = false;
    b = heap_core_alloc(&heap, 200);
    CHECK(b != NULL && growth.calls == 2 && growth.mapped == 384);
    CHECK(filled(a, 80, 0x42) && filled(b, 200, 0xcd));
    CHECK(heap_core_free(&heap, a));
    CHECK(heap_core_free(&heap, b));
    current = stats(&heap);
    CHECK(current.mapped_bytes == 384 && current.free_bytes == 336);

    a = heap_core_alloc(&heap, 900); /* Extend an existing free tail. */
    CHECK(a != NULL && growth.mapped == 1024);
    CHECK(heap_core_alloc(&heap, 100) == NULL);
    CHECK(heap_core_free(&heap, a));
    current = stats(&heap);
    CHECK(current.mapped_bytes == 1024 && current.free_bytes == 976);
    CHECK(heap_core_validate(&heap));

    start(&heap, 128, 256, 1024, false, true); /* Clamp quantum at capacity. */
    a = heap_core_alloc(&heap, 160);
    CHECK(a != NULL && growth.mapped == 256);
    CHECK(heap_core_free(&heap, a));
}

static void test_calloc_and_realloc(void)
{
    struct heap_core heap;
    start(&heap, 1024, 4096, 256, true, true);
    unsigned char *a = heap_core_calloc(&heap, 8, 7);
    CHECK(a != NULL && filled(a, 56, 0));
    for (size_t i = 0; i < 56; ++i) {
        a[i] = (unsigned char)i;
    }
    unsigned char *resized = heap_core_realloc(&heap, a, 17);
    CHECK(resized == a);
    bool content = true;
    for (size_t i = 0; i < 17; ++i) {
        content = content && resized[i] == (unsigned char)i;
    }
    CHECK(content && heap_core_validate(&heap));
    resized = heap_core_realloc(&heap, a, 200); /* Consume free successor. */
    CHECK(resized == a);
    content = true;
    for (size_t i = 0; i < 17; ++i) {
        content = content && resized[i] == (unsigned char)i;
    }
    CHECK(content && filled(resized + 17, 183, 0xcd));
    CHECK(heap_core_validate(&heap));
    struct heap_stats current = stats(&heap);
    CHECK(current.allocations == 1 && current.frees == 0 && current.used_bytes == 200);
    CHECK(heap_core_realloc(&heap, a, SIZE_MAX) == NULL);
    CHECK(heap_core_realloc(&heap, a + 16, 10) == NULL);
    CHECK(heap_core_validate(&heap));
    current = stats(&heap);
    CHECK(current.used_bytes == 200 && current.failed_allocations == 2);
    CHECK(heap_core_realloc(&heap, a, 0) == NULL);
    current = stats(&heap);
    CHECK(current.used_bytes == 0 && current.frees == 1);
    a = heap_core_realloc(&heap, NULL, 32);
    CHECK(a != NULL && heap_core_free(&heap, a));

    start(&heap, 512, 512, 128, true, false);
    a = heap_core_alloc(&heap, 32);
    void *guard = heap_core_alloc(&heap, 32);
    CHECK(a != NULL && guard != NULL);
    (void)memset(a, 0x36, 32);
    resized = heap_core_realloc(&heap, a, 128); /* Must move around used guard. */
    CHECK(resized != NULL && resized != a && filled(resized, 32, 0x36));
    CHECK(!heap_core_free(&heap, a));
    current = stats(&heap);
    CHECK(current.allocations == 3 && current.frees == 1 && current.live_allocations == 2);
    CHECK(heap_core_free(&heap, guard));
    CHECK(heap_core_free(&heap, resized));

    start(&heap, 128, 1024, 128, true, true);
    a = heap_core_alloc(&heap, 80);
    CHECK(a != NULL);
    (void)memset(a, 0x27, 80);
    growth.fail = true;
    CHECK(heap_core_realloc(&heap, a, 300) == NULL);
    CHECK(filled(a, 80, 0x27) && growth.mapped == 128);
    CHECK(heap_core_validate(&heap));
    growth.fail = false;
    resized = heap_core_realloc(&heap, a, 300); /* Grow allocated arena tail in place. */
    CHECK(resized == a && filled(resized, 80, 0x27));
    CHECK(growth.mapped == 384);
    CHECK(heap_core_validate(&heap));
    current = stats(&heap);
    CHECK(current.allocations == 1 && current.frees == 0 &&
          current.failed_allocations == 1 && current.used_bytes == 300);
    CHECK(heap_core_free(&heap, resized));
}

static void test_corruption_and_saturating_counters(void)
{
    struct heap_core heap;
    start(&heap, 256, 256, 64, false, false);
    unsigned char saved[48];
    (void)memcpy(saved, arena, sizeof(saved));
    arena[0] ^= 2u; /* Invalid low flag, not a valid allocation transition. */
    CHECK(!heap_core_validate(&heap));
    CHECK(heap_core_alloc(&heap, 1) == NULL);
    CHECK(!heap_core_free(&heap, arena + 48));
    struct heap_stats unchanged = {.mapped_bytes = 999};
    CHECK(!heap_core_get_stats(&heap, &unchanged));
    CHECK(unchanged.mapped_bytes == 999);
    (void)memcpy(arena, saved, sizeof(saved));
    CHECK(heap_core_validate(&heap));

    /* Free-list next offset is the fifth size_t in the documented header.
     * Cycle, payload target and out-of-arena target must not be dereferenced. */
    static const size_t invalid_links[] = {0, 64, SIZE_MAX - 15u};
    for (size_t i = 0; i < sizeof(invalid_links) / sizeof(invalid_links[0]); ++i) {
        (void)memcpy(arena + 4u * sizeof(size_t), &invalid_links[i], sizeof(size_t));
        CHECK(!heap_core_validate(&heap));
        CHECK(heap_core_alloc(&heap, 1) == NULL);
        (void)memcpy(arena, saved, sizeof(saved));
        CHECK(heap_core_validate(&heap));
    }

    const size_t original_head = heap.free_head;
    heap.free_head = SIZE_MAX - 15u;
    CHECK(!heap_core_validate(&heap));
    CHECK(heap_core_alloc(&heap, 1) == NULL);
    heap.free_head = original_head;
    CHECK(heap_core_validate(&heap));
    /* Damage one identity-cookie byte; validate must stop before using links. */
    arena[47] ^= 0x40u;
    CHECK(!heap_core_validate(&heap));
    CHECK(heap_core_realloc(&heap, arena + 48, 16) == NULL);
    arena[47] ^= 0x40u;
    CHECK(heap_core_validate(&heap));

    heap.allocations = UINT64_MAX;
    heap.frees = UINT64_MAX;
    heap.failed_allocations = UINT64_MAX;
    heap.invalid_frees = UINT64_MAX;
    void *pointer = heap_core_alloc(&heap, 16);
    CHECK(pointer != NULL && heap.allocations == UINT64_MAX);
    CHECK(heap_core_alloc(&heap, SIZE_MAX) == NULL);
    CHECK(heap.failed_allocations == UINT64_MAX);
    CHECK(!heap_core_free(&heap, arena));
    CHECK(heap.invalid_frees == UINT64_MAX);
    CHECK(heap_core_free(&heap, pointer));
    CHECK(heap.frees == UINT64_MAX && heap_core_validate(&heap));
}

static uint32_t random_step(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static void test_seeded_stress(void)
{
    struct heap_core heap;
    start(&heap, 4096, sizeof(arena), 4096, true, true);
    struct slot { unsigned char *pointer; size_t size; unsigned char marker; };
    struct slot slots[48] = {0};
    uint32_t seed = UINT32_C(0x5554414d);
    for (unsigned int step = 0; step < 1200; ++step) {
        const size_t index = (size_t)(random_step(&seed) % 48u);
        struct slot *slot = &slots[index];
        if (slot->pointer == NULL) {
            const size_t size = (size_t)(random_step(&seed) % 1024u) + 1u;
            unsigned char *pointer = heap_core_alloc(&heap, size);
            CHECK(pointer != NULL);
            if (pointer != NULL) {
                bool distinct = true;
                for (size_t i = 0; i < 48; ++i) {
                    if (slots[i].pointer != NULL) {
                        const uintptr_t left = (uintptr_t)pointer;
                        const uintptr_t right = (uintptr_t)slots[i].pointer;
                        distinct = distinct &&
                            (left + size <= right || right + slots[i].size <= left);
                    }
                }
                CHECK(distinct && ((uintptr_t)pointer & 15u) == 0);
                *slot = (struct slot){pointer, size, (unsigned char)(index + 1u)};
                (void)memset(pointer, slot->marker, size);
            }
        } else {
            CHECK(filled(slot->pointer, slot->size, slot->marker));
            if ((random_step(&seed) & 3u) == 0) {
                const size_t size = (size_t)(random_step(&seed) % 1536u) + 1u;
                unsigned char *replacement =
                    heap_core_realloc(&heap, slot->pointer, size);
                CHECK(replacement != NULL);
                if (replacement != NULL) {
                    const size_t preserved = size < slot->size ? size : slot->size;
                    CHECK(filled(replacement, preserved, slot->marker));
                    slot->pointer = replacement;
                    slot->size = size;
                    (void)memset(replacement, slot->marker, size);
                }
            } else {
                CHECK(heap_core_free(&heap, slot->pointer));
                *slot = (struct slot){0};
            }
        }
        CHECK(heap_core_validate(&heap));
    }
    uint64_t requested = 0;
    uint64_t live = 0;
    for (size_t i = 0; i < 48; ++i) {
        if (slots[i].pointer != NULL) {
            requested += (uint64_t)slots[i].size;
            ++live;
            CHECK(filled(slots[i].pointer, slots[i].size, slots[i].marker));
        }
    }
    struct heap_stats current = stats(&heap);
    CHECK(current.used_bytes == requested && current.live_allocations == live);
    for (size_t i = 0; i < 48; ++i) {
        CHECK(heap_core_free(&heap, slots[i].pointer));
    }
    current = stats(&heap);
    CHECK(current.used_bytes == 0 && current.live_allocations == 0);
    CHECK(current.free_bytes + 48u == current.mapped_bytes);
    CHECK(current.allocations == current.frees && current.failed_allocations == 0);
    CHECK(heap_core_validate(&heap));
}

int main(void)
{
    test_init_and_zero();
    test_alignment_split_and_oom();
    test_coalescing_and_invalid_frees();
    test_fragmentation();
    test_growth_and_rollback();
    test_calloc_and_realloc();
    test_corruption_and_saturating_counters();
    test_seeded_stress();
    (void)printf("UTAMO heap host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
