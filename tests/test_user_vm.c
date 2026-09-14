/* SPDX-License-Identifier: MIT */
/* Link the real user_vm wrapper and VMM walker to simulated PMM/HHDM/CPU.
 * No privileged instructions or physical addresses run in this host process.
 */
#include <utamo/user_vm.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
#include <utamo/paging.h>
#include <utamo/panic.h>
#include <utamo/pmm.h>
#include <utamo/string.h>
#include <stdio.h>
#include <stdlib.h>

#define MODEL_PAGES 512u
#define MODEL_BASE UINT64_C(0x100000000)
#define PAGE_BYTES ((size_t)MEMORY_PAGE_SIZE)
#define TEST_VA USER_VM_MIN

static unsigned int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

struct model {
    _Alignas(4096) unsigned char bytes[MODEL_PAGES][4096];
    bool allocated[MODEL_PAGES];
    bool nx, clone_available, interrupts, bad_if;
    unsigned int bits, allocations, frees, aliases, invalidations;
    unsigned int fail_alloc, fail_alias;
    uint64_t cr3, cr4, last_invalidated;
};
static struct model fixture;
static unsigned char input[USER_VM_COPY_LIMIT];
static unsigned char output[USER_VM_COPY_LIMIT];

static uint64_t phys_at(size_t slot)
{
    return MODEL_BASE + (uint64_t)slot * MEMORY_PAGE_SIZE;
}

static size_t phys_slot(uint64_t phys)
{
    if (phys < MODEL_BASE ||
        phys - MODEL_BASE >= MODEL_PAGES * MEMORY_PAGE_SIZE) {
        return MODEL_PAGES;
    }
    return (size_t)((phys - MODEL_BASE) / MEMORY_PAGE_SIZE);
}

static void require_if_off(void)
{
    if (fixture.interrupts) {
        fixture.bad_if = true;
    }
}

uint64_t cpu_irq_save(void)
{
    const uint64_t saved = fixture.interrupts ? UINT64_C(0x200) : 0u;
    fixture.interrupts = false;
    return saved;
}

void cpu_irq_restore(uint64_t flags)
{
    fixture.interrupts = (flags & UINT64_C(0x200)) != 0u;
}

uint64_t cpu_read_cr3(void)
{
    require_if_off();
    return fixture.cr3;
}

uint64_t cpu_read_cr4(void)
{
    require_if_off();
    return fixture.cr4;
}

void cpu_invlpg(uint64_t virt)
{
    require_if_off();
    ++fixture.invalidations;
    fixture.last_invalidated = virt;
}

bool pmm_alloc_page(uint64_t *out_phys)
{
    require_if_off();
    ++fixture.allocations;
    if (fixture.allocations == fixture.fail_alloc) {
        return false;
    }
    for (size_t i = 2u; i < MODEL_PAGES; ++i) {
        if (!fixture.allocated[i]) {
            fixture.allocated[i] = true;
            memset(fixture.bytes[i], 0xa5, PAGE_BYTES);
            *out_phys = phys_at(i);
            return true;
        }
    }
    return false;
}

bool pmm_free_page(uint64_t phys)
{
    require_if_off();
    const size_t slot = phys_slot(phys);
    if (slot < 2u || slot == MODEL_PAGES || !memory_is_page_aligned(phys) ||
        !fixture.allocated[slot]) {
        return false;
    }
    fixture.allocated[slot] = false;
    memset(fixture.bytes[slot], 0xdd, PAGE_BYTES);
    ++fixture.frees;
    return true;
}

bool pmm_is_allocated_page(uint64_t phys)
{
    require_if_off();
    const size_t slot = phys_slot(phys);
    return slot >= 2u && slot < MODEL_PAGES && memory_is_page_aligned(phys) &&
           fixture.allocated[slot];
}

bool memory_phys_to_virt(uint64_t phys, size_t bytes, void **out)
{
    require_if_off();
    ++fixture.aliases;
    const size_t slot = phys_slot(phys);
    const size_t offset = (size_t)(phys & (MEMORY_PAGE_SIZE - 1u));
    if (fixture.aliases == fixture.fail_alias || out == NULL || bytes == 0u ||
        slot >= MODEL_PAGES || !fixture.allocated[slot] ||
        bytes > PAGE_BYTES - offset) {
        return false;
    }
    *out = &fixture.bytes[slot][offset];
    return true;
}

bool vmm_get_info(struct vmm_info *out)
{
    require_if_off();
    if (out == NULL) {
        return false;
    }
    *out = (struct vmm_info){
        .root_phys = phys_at(0u), .physical_bits = fixture.bits,
        .nx_supported = fixture.nx, .nx_enabled = fixture.nx
    };
    return true;
}

bool vmm_copy_kernel_half(uint64_t out_entries[256])
{
    require_if_off();
    if (!fixture.clone_available || out_entries == NULL) {
        return false;
    }
    memset(out_entries, 0, 256u * sizeof(*out_entries));
    /* Deliberately include USER: create must sanitize even a bad snapshot. */
    out_entries[memory_page_index(VMM_DYNAMIC_BASE, 4u) - 256u] =
        phys_at(1u) | VMM_PRESENT | VMM_WRITABLE | VMM_USER;
    out_entries[255] = phys_at(1u) | VMM_PRESENT | VMM_NX;
    return true;
}

_Noreturn void kernel_panic(const char *message, const char *file,
                            unsigned int line)
{
    (void)printf("UNEXPECTED PANIC: %s (%s:%u)\n", message, file, line);
    exit(2);
}

static void reset(void)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.nx = true;
    fixture.bits = 48u;
    fixture.clone_available = true;
    fixture.interrupts = true;
    fixture.allocated[0] = true; /* Shared, reserved kernel root. */
    fixture.allocated[1] = true; /* Shared, reserved kernel subtree. */
    fixture.cr3 = phys_at(0u);
    memset(fixture.bytes[0], 0x7b, PAGE_BYTES);
    memset(fixture.bytes[1], 0x39, PAGE_BYTES);
}

static size_t live_frames(void)
{
    size_t count = 0u;
    for (size_t i = 0u; i < MODEL_PAGES; ++i) {
        count += fixture.allocated[i] ? 1u : 0u;
    }
    return count;
}

static bool filled(const unsigned char *bytes, size_t count, unsigned char value)
{
    for (size_t i = 0u; i < count; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

static uint64_t *raw_entry(const struct user_vm *vm, uint64_t virt,
                            unsigned int target)
{
    uint64_t phys = vm->space.root_phys;
    for (unsigned int level = 4u; level != 0u; --level) {
        const size_t slot = phys_slot(phys);
        if (slot >= MODEL_PAGES) {
            return NULL;
        }
        uint64_t *const table = (uint64_t *)(void *)fixture.bytes[slot];
        uint64_t *const entry = &table[memory_page_index(virt, level)];
        if (level == target) {
            return entry;
        }
        if ((*entry & VMM_PRESENT) == 0u) {
            return NULL;
        }
        phys = *entry & VMM_ADDRESS_MASK;
    }
    return NULL;
}

static void check_reclaimed(void)
{
    CHECK(live_frames() == 2u);
    CHECK(filled(fixture.bytes[0], PAGE_BYTES, 0x7bu));
    CHECK(filled(fixture.bytes[1], PAGE_BYTES, 0x39u));
    CHECK(!fixture.bad_if && fixture.interrupts);
}

static void test_create(void)
{
    reset();
    struct user_vm vm = {0};
    CHECK(!user_vm_create(NULL));
    CHECK(!user_vm_destroy(NULL));
    CHECK(!user_vm_destroy(&vm));
    CHECK(!user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
    CHECK(!user_vm_copy_from(&vm, NULL, 0u, 0u));
    fixture.nx = false;
    CHECK(!user_vm_create(&vm));
    fixture.nx = true;
    const uint64_t unsupported_cr4[] = {
        UTAMO_CR4_PCIDE, UTAMO_CR4_LA57, UTAMO_CR4_PCIDE | UTAMO_CR4_LA57
    };
    for (size_t i = 0u; i < sizeof(unsupported_cr4) / sizeof(unsupported_cr4[0]); ++i) {
        fixture.cr4 = unsupported_cr4[i];
        const struct user_vm before = vm;
        const unsigned int allocations = fixture.allocations;
        CHECK(!user_vm_create(&vm));
        CHECK(fixture.allocations == allocations && live_frames() == 2u);
        CHECK(memcmp(&before, &vm, sizeof(vm)) == 0);
        CHECK(fixture.interrupts);
    }
    fixture.cr4 = 0u;
    fixture.clone_available = false;
    CHECK(!user_vm_create(&vm));
    fixture.clone_available = true;
    CHECK(fixture.allocations == 0u);
    fixture.fail_alloc = 1u;
    CHECK(!user_vm_create(&vm));
    CHECK(!vm.initialized && vm.table_count == 0u);
    fixture.fail_alloc = 0u;
    for (unsigned int fail = 1u; fail <= 2u; ++fail) {
        fixture.aliases = 0u;
        fixture.fail_alias = fail;
        CHECK(!user_vm_create(&vm));
        CHECK(!vm.initialized && vm.table_count == 0u && live_frames() == 2u);
    }
    fixture.fail_alias = 0u;
    fixture.bits = 31u;
    CHECK(!user_vm_create(&vm));
    CHECK(!vm.initialized && live_frames() == 2u);
    fixture.bits = 48u;
    CHECK(user_vm_create(&vm));
    CHECK(vm.initialized && vm.table_count == 1u && vm.page_count == 0u);
    CHECK(vm.space.root_phys >= UINT64_C(0x100000000));
    const uint64_t *const root =
        (const uint64_t *)(const void *)fixture.bytes[phys_slot(vm.space.root_phys)];
    bool lower_zero = true;
    bool upper_supervisor = true;
    for (size_t i = 0u; i < 256u; ++i) {
        lower_zero = lower_zero && root[i] == 0u;
        upper_supervisor = upper_supervisor && (root[256u + i] & VMM_USER) == 0u;
    }
    CHECK(lower_zero && upper_supervisor);
    CHECK(root[memory_page_index(VMM_DYNAMIC_BASE, 4u)] ==
          (phys_at(1u) | VMM_PRESENT | VMM_WRITABLE));
    CHECK(root[511] == (phys_at(1u) | VMM_PRESENT | VMM_NX));
    const size_t live = live_frames();
    CHECK(!user_vm_create(&vm) && live_frames() == live);
    fixture.cr3 = vm.space.root_phys;
    CHECK(!user_vm_destroy(&vm) && live_frames() == live);
    fixture.cr3 = phys_at(0u);
    CHECK(user_vm_destroy(&vm));
    CHECK(!vm.initialized && !vm.space.initialized && vm.table_count == 0u);
    CHECK(!user_vm_destroy(&vm));
    check_reclaimed();
}

static void test_map_and_permissions(void)
{
    reset();
    struct user_vm vm = {0};
    CHECK(user_vm_create(&vm));
    struct vmm_mapping query = {.mapped = true, .physical = 123u};
    CHECK(user_vm_query(&vm, TEST_VA, &query) && !query.mapped);
    CHECK(user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
    CHECK(vm.table_count == 4u && vm.page_count == 1u && live_frames() == 7u);
    CHECK(fixture.invalidations == 0u); /* Inactive root needs no INVLPG. */
    CHECK(user_vm_query(&vm, TEST_VA + 107u, &query));
    CHECK(query.mapped && query.physical == vm.pages[0].phys + 107u);
    CHECK(query.page_size == MEMORY_PAGE_SIZE &&
          query.flags == (VMM_PRESENT | VMM_USER | VMM_WRITABLE | VMM_NX));
    CHECK(filled(fixture.bytes[phys_slot(vm.pages[0].phys)], PAGE_BYTES, 0u));
    const unsigned char hello[] = "user payload";
    CHECK(user_vm_copy_to(&vm, TEST_VA + 5u, hello, sizeof(hello)));
    memset(output, 0xa7, sizeof(output));
    CHECK(user_vm_copy_from(&vm, output, TEST_VA + 5u, sizeof(hello)));
    CHECK(memcmp(hello, output, sizeof(hello)) == 0);
    const unsigned int allocations = fixture.allocations;
    CHECK(!user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
    CHECK(fixture.allocations == allocations);
    fixture.cr3 = vm.space.root_phys;
    CHECK(user_vm_protect_page(&vm, TEST_VA, USER_VM_EXEC));
    CHECK(fixture.invalidations == 1u && fixture.last_invalidated == TEST_VA);
    CHECK(user_vm_query(&vm, TEST_VA, &query));
    CHECK(query.flags == (VMM_PRESENT | VMM_USER));
    CHECK(!user_vm_copy_to(&vm, TEST_VA, hello, sizeof(hello)));
    CHECK(user_vm_copy_from(&vm, output, TEST_VA + 5u, sizeof(hello)));
    CHECK(memcmp(hello, output, sizeof(hello)) == 0);
    CHECK(!user_vm_protect_page(&vm, TEST_VA, USER_VM_WRITE | USER_VM_EXEC));
    CHECK(user_vm_protect_page(&vm, TEST_VA, 0u));
    CHECK(user_vm_query(&vm, TEST_VA, &query));
    CHECK(query.flags == (VMM_PRESENT | VMM_USER | VMM_NX));
    CHECK(user_vm_protect_page(&vm, TEST_VA, USER_VM_WRITE));
    CHECK(user_vm_unmap_page(&vm, TEST_VA));
    CHECK(vm.page_count == 0u && vm.table_count == 4u);
    CHECK(user_vm_query(&vm, TEST_VA, &query) && !query.mapped);
    CHECK(!user_vm_unmap_page(&vm, TEST_VA));
    CHECK(!user_vm_protect_page(&vm, TEST_VA, 0u));
    CHECK(user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
    CHECK(vm.table_count == 4u);
    CHECK(filled(fixture.bytes[phys_slot(vm.pages[0].phys)], PAGE_BYTES, 0u));
    CHECK(fixture.invalidations == 5u);
    fixture.cr3 = phys_at(0u);
    fixture.interrupts = false;
    CHECK(user_vm_destroy(&vm));
    CHECK(!fixture.interrupts); /* Preserve callers that already hold IF=0. */
    fixture.interrupts = true;
    check_reclaimed();
}

static void test_invalid_ranges(void)
{
    reset();
    struct user_vm vm = {0};
    CHECK(user_vm_create(&vm));
    const uint64_t addresses[] = {
        0u, USER_VM_MIN - MEMORY_PAGE_SIZE, TEST_VA + 1u, USER_VM_END,
        UINT64_C(0xffff800000000000), UINT64_MAX
    };
    for (size_t i = 0u; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        CHECK(!user_vm_alloc_page(&vm, addresses[i], USER_VM_WRITE));
        CHECK(!user_vm_unmap_page(&vm, addresses[i]));
        CHECK(!user_vm_protect_page(&vm, addresses[i], 0u));
    }
    CHECK(!user_vm_alloc_page(&vm, TEST_VA, 4u));
    CHECK(!user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE | USER_VM_EXEC));
    CHECK(!user_vm_alloc_page(&vm, TEST_VA, UINT32_MAX));
    CHECK(vm.page_count == 0u && vm.table_count == 1u);
    struct vmm_mapping query = {.mapped = true, .physical = 123u,
                                 .flags = 456u, .page_size = 789u};
    CHECK(!user_vm_query(NULL, TEST_VA, &query));
    CHECK(!user_vm_query(&vm, TEST_VA, NULL));
    CHECK(!user_vm_query(&vm, USER_VM_END, &query));
    CHECK(query.mapped && query.physical == 123u &&
          query.flags == 456u && query.page_size == 789u);
    CHECK(user_vm_copy_from(&vm, NULL, UINT64_MAX, 0u));
    CHECK(user_vm_copy_to(&vm, UINT64_MAX, NULL, 0u));
    CHECK(!user_vm_copy_from(&vm, NULL, TEST_VA, 1u));
    CHECK(!user_vm_copy_to(&vm, TEST_VA, NULL, 1u));
    CHECK(!user_vm_copy_from(&vm, output, TEST_VA, USER_VM_COPY_LIMIT + 1u));
    CHECK(!user_vm_copy_to(&vm, TEST_VA, input, USER_VM_COPY_LIMIT + 1u));
    CHECK(!user_vm_copy_from(&vm, output, UINT64_MAX - 3u, 8u));
    CHECK(!user_vm_copy_to(&vm, USER_VM_END - 1u, input, 2u));
    CHECK(!user_vm_copy_to(&vm, USER_VM_MIN - 1u, input, 1u));
    CHECK(user_vm_alloc_page(&vm, USER_VM_END - MEMORY_PAGE_SIZE, USER_VM_WRITE));
    CHECK(user_vm_copy_to(&vm, USER_VM_END - 1u, "z", 1u));
    CHECK(user_vm_copy_from(&vm, output, USER_VM_END - 1u, 1u));
    CHECK(output[0] == (unsigned char)'z');
    CHECK(user_vm_destroy(&vm));
    check_reclaimed();
}

static void test_rollback(void)
{
    for (unsigned int failure = 1u; failure <= 4u; ++failure) {
        reset();
        struct user_vm vm = {0};
        CHECK(user_vm_create(&vm));
        fixture.allocations = 0u;
        fixture.fail_alloc = failure; /* Data, then three intermediate tables. */
        CHECK(!user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
        CHECK(vm.page_count == 0u && vm.table_count == 1u && live_frames() == 3u);
        struct vmm_mapping query;
        CHECK(user_vm_query(&vm, TEST_VA, &query) && !query.mapped);
        fixture.fail_alloc = 0u;
        CHECK(user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
        CHECK(user_vm_destroy(&vm));
        check_reclaimed();
    }
    for (unsigned int failure = 1u; failure <= 6u; ++failure) {
        reset();
        struct user_vm vm = {0};
        CHECK(user_vm_create(&vm));
        fixture.aliases = 0u;
        fixture.fail_alias = failure; /* Query, data, map, and private tables. */
        CHECK(!user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
        CHECK(vm.page_count == 0u && vm.table_count == 1u && live_frames() == 3u);
        fixture.fail_alias = 0u;
        struct vmm_mapping query;
        CHECK(user_vm_query(&vm, TEST_VA, &query) && !query.mapped);
        CHECK(user_vm_alloc_page(&vm, TEST_VA, USER_VM_WRITE));
        CHECK(user_vm_destroy(&vm));
        check_reclaimed();
    }
}

static void test_isolation_and_ownership(void)
{
    reset();
    struct user_vm first = {0}, second = {0};
    CHECK(user_vm_create(&first) && user_vm_create(&second));
    CHECK(user_vm_alloc_page(&first, TEST_VA, USER_VM_WRITE));
    CHECK(user_vm_alloc_page(&second, TEST_VA, USER_VM_WRITE));
    CHECK(first.space.root_phys != second.space.root_phys);
    CHECK(first.pages[0].phys != second.pages[0].phys);
    const unsigned char first_marker = 0x36u, second_marker = 0x82u;
    CHECK(user_vm_copy_to(&first, TEST_VA, &first_marker, 1u));
    CHECK(user_vm_copy_from(&second, output, TEST_VA, 1u) && output[0] == 0u);
    CHECK(user_vm_copy_to(&second, TEST_VA, &second_marker, 1u));
    CHECK(user_vm_copy_from(&first, output, TEST_VA, 1u) &&
          output[0] == first_marker);
    uint64_t *const leaf = raw_entry(&first, TEST_VA, 1u);
    CHECK(leaf != NULL);
    if (leaf != NULL) {
        const uint64_t original = *leaf;
        const uint64_t foreign[] = {second.pages[0].phys, first.space.root_phys,
                                    phys_at(0u), phys_at(1u)};
        for (size_t i = 0u; i < sizeof(foreign) / sizeof(foreign[0]); ++i) {
            *leaf = foreign[i] | VMM_PRESENT | VMM_USER | VMM_WRITABLE | VMM_NX;
            struct vmm_mapping query = {.physical = 123u};
            CHECK(!user_vm_query(&first, TEST_VA, &query));
            CHECK(query.physical == 123u);
            CHECK(!user_vm_copy_to(&first, TEST_VA, "x", 1u));
        }
        *leaf = original & ~VMM_USER;
        CHECK(!user_vm_copy_from(&first, output, TEST_VA, 1u));
        *leaf = original & ~VMM_NX; /* A corrupted W+X leaf is never accepted. */
        CHECK(!user_vm_copy_from(&first, output, TEST_VA, 1u));
        *leaf = original | VMM_GLOBAL;
        CHECK(!user_vm_copy_from(&first, output, TEST_VA, 1u));
        *leaf = original;
    }
    uint64_t *const parent = raw_entry(&first, TEST_VA, 4u);
    CHECK(parent != NULL);
    if (parent != NULL) {
        const uint64_t original = *parent;
        *parent = second.tables[1].phys | VMM_PRESENT | VMM_USER | VMM_WRITABLE;
        CHECK(!user_vm_copy_from(&first, output, TEST_VA, 1u));
        *parent = original & ~VMM_USER;
        CHECK(!user_vm_copy_from(&first, output, TEST_VA, 1u));
        *parent = original;
    }
    const size_t live = live_frames();
    const uint64_t original_phys = first.tables[1].phys;
    first.tables[1].phys = first.tables[0].phys;
    CHECK(!user_vm_destroy(&first) && live_frames() == live);
    first.tables[1].phys = original_phys;
    CHECK(user_vm_destroy(&first));
    CHECK(second.initialized);
    CHECK(user_vm_copy_from(&second, output, TEST_VA, 1u) &&
          output[0] == second_marker);
    CHECK(user_vm_destroy(&second));
    check_reclaimed();
}

static void test_atomic_copy(void)
{
    reset();
    struct user_vm vm = {0};
    CHECK(user_vm_create(&vm));
    for (size_t i = 0u; i < 17u; ++i) {
        CHECK(user_vm_alloc_page(&vm, TEST_VA + (uint64_t)i * MEMORY_PAGE_SIZE,
                                 USER_VM_WRITE));
    }
    for (size_t i = 0u; i < sizeof(input); ++i) {
        input[i] = (unsigned char)((i * 73u + i / 4096u) & 255u);
    }
    fixture.aliases = 0u;
    fixture.fail_alias = 86u; /* 17*(four table walks + one cached data alias). */
    CHECK(user_vm_copy_to(&vm, TEST_VA + 3u, input, sizeof(input)));
    CHECK(fixture.aliases == 85u); /* No fallible translation after first copy. */
    fixture.fail_alias = 0u;
    CHECK(user_vm_copy_from(&vm, output, TEST_VA + 3u, sizeof(output)));
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    for (unsigned int chunk = 1u; chunk <= 17u; ++chunk) {
        memset(output, 0xcc, sizeof(output));
        fixture.aliases = 0u;
        fixture.fail_alias = chunk * 5u;
        CHECK(!user_vm_copy_from(&vm, output, TEST_VA + 3u, sizeof(output)));
        CHECK(filled(output, sizeof(output), 0xccu));
        fixture.fail_alias = 0u;
        /* Input changes must not reach even the first user page on failure. */
        const unsigned char prior = input[0];
        input[0] ^= 0xffu;
        fixture.aliases = 0u;
        fixture.fail_alias = chunk * 5u;
        CHECK(!user_vm_copy_to(&vm, TEST_VA + 3u, input, sizeof(input)));
        fixture.fail_alias = 0u;
        CHECK(user_vm_copy_from(&vm, output, TEST_VA + 3u, sizeof(output)));
        CHECK(output[0] == prior);
        input[0] = prior;
        CHECK(memcmp(input, output, sizeof(input)) == 0);
    }
    CHECK(user_vm_protect_page(&vm, TEST_VA + MEMORY_PAGE_SIZE, 0u));
    memset(output, 0, 16u);
    CHECK(user_vm_copy_from(&vm, output, TEST_VA + MEMORY_PAGE_SIZE - 8u, 16u));
    unsigned char original[16];
    memcpy(original, output, sizeof(original));
    CHECK(!user_vm_copy_to(&vm, TEST_VA + MEMORY_PAGE_SIZE - 8u, "replacement data",
                           16u));
    CHECK(user_vm_copy_from(&vm, output, TEST_VA + MEMORY_PAGE_SIZE - 8u, 16u));
    CHECK(memcmp(original, output, sizeof(original)) == 0);
    CHECK(user_vm_unmap_page(&vm, TEST_VA + MEMORY_PAGE_SIZE));
    memset(output, 0xcc, 16u);
    CHECK(!user_vm_copy_from(&vm, output, TEST_VA + MEMORY_PAGE_SIZE - 8u, 16u));
    CHECK(filled(output, 16u, 0xccu));
    CHECK(!user_vm_copy_to(&vm, TEST_VA + MEMORY_PAGE_SIZE - 8u, input, 16u));
    CHECK(user_vm_destroy(&vm));
    check_reclaimed();
}

static void test_capacity_and_reuse(void)
{
    reset();
    struct user_vm vm = {0};
    CHECK(user_vm_create(&vm));
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; ++i) {
        CHECK(user_vm_alloc_page(&vm, TEST_VA + (uint64_t)i * MEMORY_PAGE_SIZE,
                                 i % 2u == 0u ? USER_VM_WRITE : USER_VM_EXEC));
    }
    CHECK(vm.page_count == USER_VM_PAGE_LIMIT && vm.table_count == 4u);
    const size_t full = live_frames();
    CHECK(!user_vm_alloc_page(&vm, TEST_VA +
          USER_VM_PAGE_LIMIT * MEMORY_PAGE_SIZE, USER_VM_WRITE));
    CHECK(live_frames() == full);
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; i += 2u) {
        CHECK(user_vm_unmap_page(&vm, TEST_VA + (uint64_t)i * MEMORY_PAGE_SIZE));
    }
    CHECK(vm.page_count == USER_VM_PAGE_LIMIT / 2u);
    for (size_t i = 0u; i < USER_VM_PAGE_LIMIT; i += 2u) {
        CHECK(user_vm_alloc_page(&vm, TEST_VA + (uint64_t)i * MEMORY_PAGE_SIZE,
                                 USER_VM_WRITE));
    }
    CHECK(live_frames() == full && vm.table_count == 4u);
    CHECK(user_vm_destroy(&vm));
    check_reclaimed();

    CHECK(user_vm_create(&vm));
    for (size_t i = 0u; i < 20u; ++i) {
        /* Different PML4 slots consume three private tables per mapping. */
        CHECK(user_vm_alloc_page(&vm, TEST_VA + ((uint64_t)i << 39u),
                                 USER_VM_WRITE));
    }
    CHECK(vm.table_count == 61u && vm.page_count == 20u);
    CHECK(user_vm_alloc_page(&vm, TEST_VA + UINT64_C(0x200000), USER_VM_WRITE));
    CHECK(vm.table_count == 62u && vm.page_count == 21u);
    const size_t partial_live = live_frames();
    /* Two available slots cannot commit a three-table chain. Both provisional
     * tables and the data frame must roll back when the third slot is refused. */
    CHECK(!user_vm_alloc_page(&vm, TEST_VA + (UINT64_C(20) << 39u), USER_VM_WRITE));
    CHECK(vm.table_count == 62u && vm.page_count == 21u &&
          live_frames() == partial_live);
    CHECK(user_vm_alloc_page(&vm, TEST_VA + UINT64_C(0x400000), USER_VM_WRITE));
    CHECK(user_vm_alloc_page(&vm, TEST_VA + UINT64_C(0x600000), USER_VM_WRITE));
    CHECK(vm.table_count == USER_VM_TABLE_LIMIT && vm.page_count == 23u);
    const size_t table_full = live_frames();
    CHECK(!user_vm_alloc_page(&vm, TEST_VA + (UINT64_C(21) << 39u), USER_VM_WRITE));
    CHECK(vm.table_count == USER_VM_TABLE_LIMIT && vm.page_count == 23u);
    CHECK(live_frames() == table_full);
    /* An existing PT still accepts another page with no new table allocation. */
    CHECK(user_vm_alloc_page(&vm, TEST_VA + MEMORY_PAGE_SIZE, USER_VM_WRITE));
    CHECK(user_vm_destroy(&vm));
    check_reclaimed();

    for (size_t cycle = 0u; cycle < 12u; ++cycle) {
        CHECK(user_vm_create(&vm));
        for (size_t i = 0u; i < 8u; ++i) {
            const uint64_t virt = TEST_VA + (uint64_t)i * MEMORY_PAGE_SIZE;
            CHECK(user_vm_alloc_page(&vm, virt, USER_VM_WRITE));
            const uint64_t marker = UINT64_C(0xa5a5000000000000) |
                                     (uint64_t)(cycle * 8u + i);
            uint64_t readback = 0u;
            CHECK(user_vm_copy_to(&vm, virt + 13u, &marker, sizeof(marker)));
            CHECK(user_vm_copy_from(&vm, &readback, virt + 13u, sizeof(readback)));
            CHECK(readback == marker);
        }
        CHECK(user_vm_destroy(&vm));
        check_reclaimed();
    }
}

int main(void)
{
    test_create();
    test_map_and_permissions();
    test_invalid_ranges();
    test_rollback();
    test_isolation_and_ownership();
    test_atomic_copy();
    test_capacity_and_reuse();
    (void)printf("UTAMO user VM host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
