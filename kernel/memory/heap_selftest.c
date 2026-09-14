/* SPDX-License-Identifier: MIT */
#include <utamo/heap.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/vmm.h>
#include <utamo/log.h>
#include <utamo/string.h>

#define UTAMO_HEAP_TEST_SLOTS 128u
#define UTAMO_HEAP_TEST_OPERATIONS 8192u
#define UTAMO_HEAP_TEST_SEED UINT32_C(0x41535452)

struct heap_test_slot {
    unsigned char *pointer;
    size_t bytes;
    unsigned char marker;
};

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static unsigned char expected_byte(unsigned char marker, size_t offset)
{
    return (unsigned char)(marker ^ (unsigned char)offset ^
                           (unsigned char)(offset >> 8u));
}

static bool verify_bytes(const struct heap_test_slot *slot, size_t bytes)
{
    for (size_t i = 0u; i < bytes; ++i) {
        if (slot->pointer[i] != expected_byte(slot->marker, i)) {
            return false;
        }
    }
    return true;
}

static void fill_bytes(struct heap_test_slot *slot)
{
    for (size_t i = 0u; i < slot->bytes; ++i) {
        slot->pointer[i] = expected_byte(slot->marker, i);
    }
}

static bool unique_allocation(const struct heap_test_slot *slots, size_t index)
{
    const uintptr_t start = (uintptr_t)slots[index].pointer;
    if (start == 0u || (start & 15u) != 0u ||
        slots[index].bytes > UINTPTR_MAX - start) {
        return false;
    }
    const uintptr_t end = start + slots[index].bytes;
    for (size_t j = 0u; j < UTAMO_HEAP_TEST_SLOTS; ++j) {
        if (j == index || slots[j].pointer == NULL) {
            continue;
        }
        const uintptr_t other = (uintptr_t)slots[j].pointer;
        if (slots[j].bytes > UINTPTR_MAX - other ||
            (start < other + slots[j].bytes && other < end)) {
            return false;
        }
    }
    return true;
}

bool heap_selftest(void)
{
    struct heap_stats before, after;
    struct pmm_stats pmm_before, pmm_after;
    struct vmm_info vmm_before, vmm_after;
    struct heap_test_slot slots[UTAMO_HEAP_TEST_SLOTS] = {{0}};
    uint32_t random = UTAMO_HEAP_TEST_SEED;
    bool good = heap_validate() && heap_get_stats(&before) &&
                pmm_get_stats(&pmm_before) && vmm_get_info(&vmm_before);
    if (!good) {
        return false;
    }
    kprintf("Heap stress seed: 0x%llx\n",
            (unsigned long long)UTAMO_HEAP_TEST_SEED);
    good = kmalloc(0u) == NULL && kcalloc(SIZE_MAX, 2u) == NULL &&
           kmalloc(SIZE_MAX) == NULL && kfree(NULL) && !kfree(&random);
    for (size_t i = 0u; good && i < UTAMO_HEAP_TEST_SLOTS; ++i) {
        slots[i].bytes = 4096u + (size_t)(random_next(&random) & UINT32_C(2047));
        slots[i].marker = (unsigned char)random_next(&random);
        slots[i].pointer = kmalloc(slots[i].bytes);
        good = unique_allocation(slots, i);
        if (good) {
            fill_bytes(&slots[i]);
        }
    }
    if (good) {
        good = !kfree(slots[0].pointer + 1u) &&
               krealloc(slots[0].pointer, SIZE_MAX) == NULL &&
               verify_bytes(&slots[0], slots[0].bytes);
    }
    /* Explicit alternating holes exercise forward/backward coalescence. */
    for (size_t i = 1u; good && i < UTAMO_HEAP_TEST_SLOTS; i += 2u) {
        unsigned char *old = slots[i].pointer;
        good = verify_bytes(&slots[i], slots[i].bytes) && kfree(old);
        if (good) {
            slots[i].pointer = NULL;
            good = !kfree(old);
        }
    }
    size_t operation = 0u;
    for (; good && operation < UTAMO_HEAP_TEST_OPERATIONS; ++operation) {
        const uint32_t value = random_next(&random);
        const size_t index = (size_t)(value % UTAMO_HEAP_TEST_SLOTS);
        struct heap_test_slot *slot = &slots[index];
        if (slot->pointer != NULL && !verify_bytes(slot, slot->bytes)) {
            good = false;
            break;
        }
        if (slot->pointer != NULL && ((value >> 8u) & UINT32_C(3)) == 0u) {
            good = kfree(slot->pointer);
            if (good) {
                slot->pointer = NULL;
            }
            continue;
        }
        const size_t size = 1u + (size_t)(random_next(&random) & UINT32_C(8191));
        if (slot->pointer == NULL) {
            slot->pointer = kcalloc(size, 1u);
            slot->bytes = size;
            good = unique_allocation(slots, index);
            for (size_t i = 0u; good && i < size; ++i) {
                good = slot->pointer[i] == 0u;
            }
        } else {
            const size_t preserved = size < slot->bytes ? size : slot->bytes;
            unsigned char *changed = krealloc(slot->pointer, size);
            if (changed == NULL) {
                good = false;
                break;
            }
            slot->pointer = changed;
            slot->bytes = size;
            good = unique_allocation(slots, index) && verify_bytes(slot, preserved);
        }
        if (good) {
            slot->marker = (unsigned char)random_next(&random);
            fill_bytes(slot);
        }
        if ((operation & 255u) == 0u && !heap_validate()) {
            good = false;
        }
    }
    for (size_t i = 0u; i < UTAMO_HEAP_TEST_SLOTS; ++i) {
        if (slots[i].pointer != NULL) {
            if (!verify_bytes(&slots[i], slots[i].bytes)) {
                good = false;
            }
            if (!kfree(slots[i].pointer)) {
                good = false;
            }
        }
    }
    kprintf("Heap stress operations: %llu\n", (unsigned long long)operation);
    if (!heap_validate() || !heap_get_stats(&after) ||
        !pmm_get_stats(&pmm_after) || !vmm_get_info(&vmm_after) ||
        after.used_bytes != before.used_bytes ||
        after.live_allocations != before.live_allocations ||
        after.mapped_bytes < before.mapped_bytes ||
        pmm_after.used_frames < pmm_before.used_frames ||
        vmm_after.table_pages < vmm_before.table_pages ||
        pmm_after.used_frames - pmm_before.used_frames !=
            (after.mapped_bytes - before.mapped_bytes) / MEMORY_PAGE_SIZE +
            vmm_after.table_pages - vmm_before.table_pages) {
        good = false;
    }
    return good;
}
