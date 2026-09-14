/* SPDX-License-Identifier: MIT */
/* Host doubles only: no privileged instruction, real physical RAM or VM. */
#include <utamo/thread_stack.h>
#include <utamo/cpu.h>
#include <utamo/memory.h>
#include <utamo/panic.h>
#include <utamo/pmm.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define FRAME_COUNT (UTAMO_THREAD_STACK_SLOTS * 16u)
#define SLOT_PAGES (UTAMO_THREAD_STACK_SLOTS * 17u)
#define PHYSICAL_BASE UINT64_C(0x200000)

static unsigned int checks, failures;
/* Callback assertions are accumulated per scenario, not counted per event. */
static bool callback_invariants = true;
static bool irq_enabled = true;
static bool nx_enabled = true;
static bool owned[FRAME_COUNT];
static _Alignas(16) unsigned char physical_memory[FRAME_COUNT][4096];
static struct vmm_mapping virtual_pages[SLOT_PAGES];
static unsigned int alloc_calls, alias_calls, map_calls, query_calls, unmap_calls;
static unsigned int fail_alloc, fail_alias, fail_map, fail_query, fail_unmap;
static jmp_buf panic_target;
static bool panic_expected;
static const char *panic_message;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

static void observe(bool condition, const char *expression, unsigned int line)
{
    if (!condition) {
        callback_invariants = false;
        (void)printf("FIXTURE FAIL line %u: %s\n", line, expression);
    }
}
#define OBSERVE(expression) observe((expression), #expression, __LINE__)

uint64_t cpu_irq_save(void)
{
    const uint64_t result = irq_enabled ? UINT64_C(0x200) : 0u;
    irq_enabled = false;
    return result;
}

void cpu_irq_restore(uint64_t flags)
{
    irq_enabled = (flags & UINT64_C(0x200)) != 0u;
}

_Noreturn void kernel_panic(const char *message, const char *file, unsigned int line)
{
    (void)file;
    (void)line;
    panic_message = message;
    OBSERVE(panic_expected);
    longjmp(panic_target, 1);
}

static size_t physical_index(uint64_t phys)
{
    if (phys < PHYSICAL_BASE || !memory_is_page_aligned(phys)) {
        return FRAME_COUNT;
    }
    const uint64_t index = (phys - PHYSICAL_BASE) / MEMORY_PAGE_SIZE;
    return index < FRAME_COUNT ? (size_t)index : FRAME_COUNT;
}

static size_t virtual_index(uint64_t virt)
{
    if (virt < UTAMO_THREAD_STACK_REGION || !memory_is_page_aligned(virt)) {
        return SLOT_PAGES;
    }
    const uint64_t index = (virt - UTAMO_THREAD_STACK_REGION) / MEMORY_PAGE_SIZE;
    return index < SLOT_PAGES ? (size_t)index : SLOT_PAGES;
}

bool pmm_alloc_page(uint64_t *out)
{
    OBSERVE(!irq_enabled);
    ++alloc_calls;
    if (alloc_calls == fail_alloc) {
        return false;
    }
    for (size_t i = 0u; i < FRAME_COUNT; ++i) {
        if (!owned[i]) {
            owned[i] = true;
            memset(physical_memory[i], 0xa5, 4096u);
            *out = PHYSICAL_BASE + (uint64_t)i * MEMORY_PAGE_SIZE;
            return true;
        }
    }
    return false;
}

bool pmm_is_allocated_page(uint64_t phys)
{
    OBSERVE(!irq_enabled);
    const size_t index = physical_index(phys);
    return index < FRAME_COUNT && owned[index];
}

bool pmm_free_page(uint64_t phys)
{
    OBSERVE(!irq_enabled);
    const size_t index = physical_index(phys);
    OBSERVE(index < FRAME_COUNT && owned[index]);
    if (index >= FRAME_COUNT || !owned[index]) {
        return false;
    }
    for (size_t i = 0u; i < SLOT_PAGES; ++i) {
        if (virtual_pages[i].mapped && virtual_pages[i].physical == phys) {
            OBSERVE(false);
            return false;
        }
    }
    owned[index] = false;
    return true;
}

bool memory_phys_to_virt(uint64_t phys, size_t bytes, void **out)
{
    OBSERVE(!irq_enabled);
    ++alias_calls;
    if (alias_calls == fail_alias) {
        return false;
    }
    const size_t index = physical_index(phys);
    OBSERVE(index < FRAME_COUNT && owned[index] && bytes == 4096u);
    if (index >= FRAME_COUNT || !owned[index] || bytes != 4096u) {
        return false;
    }
    *out = physical_memory[index];
    return true;
}

bool vmm_get_info(struct vmm_info *out)
{
    OBSERVE(!irq_enabled);
    *out = (struct vmm_info){.nx_supported = nx_enabled, .nx_enabled = nx_enabled};
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

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    OBSERVE(!irq_enabled);
    ++map_calls;
    if (map_calls == fail_map) {
        return false;
    }
    const size_t slot = virtual_index(virt);
    const size_t frame = physical_index(phys);
    OBSERVE(slot < SLOT_PAGES && slot % 17u != 0u && !virtual_pages[slot].mapped);
    OBSERVE(frame < FRAME_COUNT && owned[frame]);
    if (slot >= SLOT_PAGES || slot % 17u == 0u ||
        virtual_pages[slot].mapped || frame >= FRAME_COUNT || !owned[frame]) {
        return false;
    }
    OBSERVE(flags == (VMM_PRESENT | VMM_WRITABLE | (nx_enabled ? VMM_NX : 0u)));
    OBSERVE(all_zero(physical_memory[frame]));
    virtual_pages[slot] = (struct vmm_mapping){
        .mapped = true, .physical = phys, .flags = flags, .page_size = MEMORY_PAGE_SIZE
    };
    return true;
}

bool vmm_query_page(uint64_t virt, struct vmm_mapping *out)
{
    OBSERVE(!irq_enabled);
    ++query_calls;
    if (query_calls == fail_query) {
        return false;
    }
    const size_t index = virtual_index(virt);
    OBSERVE(index < SLOT_PAGES);
    if (index >= SLOT_PAGES) {
        return false;
    }
    *out = virtual_pages[index];
    return true;
}

bool vmm_unmap_page(uint64_t virt)
{
    OBSERVE(!irq_enabled);
    ++unmap_calls;
    if (unmap_calls == fail_unmap) {
        return false;
    }
    const size_t index = virtual_index(virt);
    OBSERVE(index < SLOT_PAGES && index % 17u != 0u && virtual_pages[index].mapped);
    if (index >= SLOT_PAGES || !virtual_pages[index].mapped) {
        return false;
    }
    virtual_pages[index] = (struct vmm_mapping){0};
    return true;
}

static size_t allocated_frames(void)
{
    size_t count = 0u;
    for (size_t i = 0u; i < FRAME_COUNT; ++i) {
        if (owned[i]) {
            ++count;
        }
    }
    return count;
}

static void reset_failures(void)
{
    fail_alloc = 0u;
    fail_alias = 0u;
    fail_map = 0u;
    fail_query = 0u;
    fail_unmap = 0u;
}

static void check_empty(void)
{
    CHECK(allocated_frames() == 0u);
    bool unmapped = true;
    for (size_t i = 0u; i < SLOT_PAGES; ++i) {
        if (virtual_pages[i].mapped) {
            unmapped = false;
        }
    }
    CHECK(unmapped); /* Every mapping was examined; one invariant per scenario. */
    CHECK(irq_enabled);
    CHECK(callback_invariants);
    callback_invariants = true;
}

static void test_failure_positions(void)
{
    for (unsigned int kind = 0u; kind < 3u; ++kind) {
        for (unsigned int position = 1u; position <= 16u; ++position) {
            struct thread_stack stack = {.top = UINT64_C(0x55aa)};
            if (kind == 0u) {
                fail_alloc = alloc_calls + position;
            } else if (kind == 1u) {
                fail_alias = alias_calls + position;
            } else {
                fail_map = map_calls + position;
            }
            CHECK(!thread_stack_alloc(&stack));
            CHECK(stack.top == UINT64_C(0x55aa) && !stack.active);
            reset_failures();
            check_empty();
            CHECK(thread_stack_alloc(&stack)); /* Same slot remains reusable. */
            CHECK(stack.slot == 0u && stack.active);
            CHECK(thread_stack_free(&stack));
        }
    }
    for (unsigned int position = 1u; position <= 17u; ++position) {
        struct thread_stack stack = {0};
        fail_query = query_calls + position; /* guard, then 16-page preflight. */
        const unsigned int before = alloc_calls;
        CHECK(!thread_stack_alloc(&stack));
        CHECK(!stack.active && alloc_calls == before);
        reset_failures();
        check_empty();
    }
    for (size_t page = 0u; page < 17u; ++page) {
        virtual_pages[page] = (struct vmm_mapping){
            .mapped = true, .physical = UINT64_C(0xbeef000),
            .flags = VMM_PRESENT, .page_size = MEMORY_PAGE_SIZE
        };
        struct thread_stack stack = {0};
        const unsigned int before = alloc_calls;
        CHECK(!thread_stack_alloc(&stack));
        CHECK(alloc_calls == before && !stack.active);
        CHECK(virtual_pages[page].mapped &&
              virtual_pages[page].physical == UINT64_C(0xbeef000));
        virtual_pages[page] = (struct vmm_mapping){0};
    }
    check_empty();
}

static void test_frames(void)
{
    for (unsigned int nx = 0u; nx < 2u; ++nx) {
        nx_enabled = nx != 0u;
        struct thread_stack stack;
        CHECK(thread_stack_alloc(&stack));
        CHECK(stack.guard == UTAMO_THREAD_STACK_REGION);
        CHECK(stack.base == stack.guard + MEMORY_PAGE_SIZE);
        CHECK(stack.top == stack.base + UTAMO_THREAD_STACK_SIZE);
        CHECK(!virtual_pages[0].mapped);
        CHECK(allocated_frames() == 16u);
        struct interrupt_frame *frame_pointer = NULL;
        const uint64_t entry = UINT64_C(0xffffffff80001235);
        const uint64_t sentinel = UINT64_C(0xffffffff80004567);
        CHECK(!thread_frame_init(NULL, entry, sentinel, &frame_pointer));
        CHECK(!thread_frame_init(&stack, 0u, sentinel, &frame_pointer));
        CHECK(!thread_frame_init(&stack, entry, 0u, &frame_pointer));
        CHECK(!thread_frame_init(&stack, UINT64_C(0x800000000000),
                                  sentinel, &frame_pointer));
        CHECK(!thread_frame_init(&stack, entry, UINT64_C(0xffff7fffffffffff),
                                  &frame_pointer));
        CHECK(!thread_frame_init(&stack, entry, sentinel, NULL));
        CHECK(frame_pointer == NULL);
        fail_alias = alias_calls + 1u;
        CHECK(!thread_frame_init(&stack, entry, sentinel, &frame_pointer));
        CHECK(frame_pointer == NULL);
        reset_failures();
        CHECK(thread_frame_init(&stack, entry, sentinel, &frame_pointer));
        const uint64_t frame_address = (uint64_t)(uintptr_t)frame_pointer;
        CHECK(frame_address == stack.top - 192u && frame_address % 16u == 0u);
        const size_t frame = physical_index(virtual_pages[16].physical);
        struct interrupt_frame captured;
        memcpy(&captured, physical_memory[frame] + 3904u, sizeof(captured));
        const struct interrupt_frame expected = {
            .rip = entry, .cs = 8u, .ss = 16u,
            .rflags = UINT64_C(0x202), .rsp = stack.top - 8u
        };
        CHECK(memcmp(&captured, &expected, sizeof(captured)) == 0);
        CHECK(captured.rsp % 16u == 8u);
        uint64_t return_slot;
        memcpy(&return_slot, physical_memory[frame] + 4088u, sizeof(return_slot));
        CHECK(return_slot == sentinel);
        CHECK(thread_stack_free(&stack));
        CHECK(!stack.active && stack.top == 0u && stack.generation == 0u);
        CHECK(!thread_stack_free(&stack));
        check_empty();
    }
    nx_enabled = true;
}

static void test_lifetime(void)
{
    CHECK(!thread_stack_alloc(NULL));
    CHECK(!thread_stack_free(NULL));
    struct thread_stack stack;
    CHECK(thread_stack_alloc(&stack));
    struct thread_stack old = stack;
    struct thread_stack invalid = stack;
    invalid.slot = 64u;
    CHECK(!thread_stack_free(&invalid));
    invalid = stack;
    ++invalid.base;
    CHECK(!thread_stack_free(&invalid));
    invalid = stack;
    ++invalid.generation;
    CHECK(!thread_stack_free(&invalid));
    CHECK(allocated_frames() == 16u && stack.active);
    CHECK(thread_stack_free(&stack));
    CHECK(!thread_stack_free(&old));
    CHECK(thread_stack_alloc(&stack));
    CHECK(stack.slot == old.slot && stack.generation > old.generation);
    CHECK(!thread_stack_free(&old));
    struct interrupt_frame *frame = NULL;
    CHECK(!thread_frame_init(&old, 1u, 2u, &frame));
    CHECK(allocated_frames() == 16u);
    CHECK(thread_stack_free(&stack));
    check_empty();

    struct thread_stack stacks[UTAMO_THREAD_STACK_SLOTS];
    for (size_t i = 0u; i < UTAMO_THREAD_STACK_SLOTS; ++i) {
        CHECK(thread_stack_alloc(&stacks[i]));
        CHECK(stacks[i].slot == i && stacks[i].active);
        CHECK(stacks[i].guard ==
              UTAMO_THREAD_STACK_REGION + (uint64_t)i * UTAMO_THREAD_STACK_STRIDE);
        CHECK(!virtual_pages[i * 17u].mapped);
    }
    struct thread_stack extra = {.base = UINT64_C(0x99)};
    CHECK(!thread_stack_alloc(&extra));
    CHECK(extra.base == UINT64_C(0x99));
    CHECK(allocated_frames() == FRAME_COUNT);
    for (size_t i = 0u; i < UTAMO_THREAD_STACK_SLOTS; i += 2u) {
        CHECK(thread_stack_free(&stacks[i]));
    }
    CHECK(allocated_frames() == FRAME_COUNT / 2u);
    for (size_t i = 0u; i < UTAMO_THREAD_STACK_SLOTS; i += 2u) {
        CHECK(thread_stack_alloc(&stacks[i]));
        CHECK(stacks[i].slot == i);
    }
    for (size_t i = UTAMO_THREAD_STACK_SLOTS; i != 0u; --i) {
        CHECK(thread_stack_free(&stacks[i - 1u]));
    }
    check_empty();
}

static void test_fatal_cleanup(void)
{
    struct thread_stack stack;
    CHECK(thread_stack_alloc(&stack));
    fail_unmap = unmap_calls + 1u;
    panic_expected = true;
    if (setjmp(panic_target) == 0) {
        (void)thread_stack_free(&stack);
        CHECK(false);
    } else {
        CHECK(panic_message != NULL);
        CHECK(!irq_enabled);
        CHECK(stack.active && allocated_frames() == 16u);
        CHECK(virtual_pages[1].mapped);
    }
    /* Host-only repair after intercepted fatal path; a kernel never resumes. */
    panic_expected = false;
    reset_failures();
    irq_enabled = true;
    CHECK(thread_stack_free(&stack));
    check_empty();
}

int main(void)
{
    test_failure_positions();
    test_frames();
    test_lifetime();
    test_fatal_cleanup();
    (void)printf("UTAMO thread stack host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
