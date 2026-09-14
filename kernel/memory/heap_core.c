/* SPDX-License-Identifier: MIT */
#include <utamo/heap_core.h>
#include <utamo/string.h>

#define UTAMO_HEAP_USED ((size_t)1)
#define UTAMO_HEAP_FLAG_MASK ((size_t)15)
#define UTAMO_HEAP_NONE SIZE_MAX
#define UTAMO_HEAP_COOKIE UINT64_C(0x5554414d4f484541)
#define UTAMO_HEAP_MIN_BLOCK ((size_t)64)

/* prev_size is a boundary tag. Links are offsets, never unchecked pointers.
 * The cookie detects obvious corruption; it is not a security boundary.
 */
struct heap_block {
    size_t size_flags;
    size_t prev_size;
    size_t requested;
    size_t free_prev;
    size_t free_next;
    uintptr_t cookie;
};
_Static_assert(sizeof(struct heap_block) == 48, "64-bit heap block layout");
_Static_assert(sizeof(size_t) == sizeof(uint64_t), "x86_64 heap sizes");

static void increment(uint64_t *counter)
{
    if (*counter != UINT64_MAX) {
        ++*counter;
    }
}

static size_t block_size(const struct heap_block *block)
{
    return block->size_flags & ~UTAMO_HEAP_FLAG_MASK;
}

static bool block_used(const struct heap_block *block)
{
    return (block->size_flags & UTAMO_HEAP_USED) != 0;
}

static struct heap_block *block_at(const struct heap_core *heap, size_t offset)
{
    return (struct heap_block *)(void *)(heap->base + offset);
}

static uintptr_t block_cookie(const struct heap_core *heap, size_t offset)
{
    return (uintptr_t)UTAMO_HEAP_COOKIE ^ (uintptr_t)heap->base ^ (uintptr_t)offset;
}

static void write_block(struct heap_core *heap, size_t offset, size_t size,
                        size_t previous, size_t requested, bool used)
{
    *block_at(heap, offset) = (struct heap_block){
        .size_flags = size | (used ? UTAMO_HEAP_USED : 0u),
        .prev_size = previous,
        .requested = requested,
        .free_prev = UTAMO_HEAP_NONE,
        .free_next = UTAMO_HEAP_NONE,
        .cookie = block_cookie(heap, offset)
    };
}

static bool context_valid(const struct heap_core *heap)
{
    return heap != NULL && heap->initialized && heap->base != NULL &&
           ((uintptr_t)heap->base & UTAMO_HEAP_FLAG_MASK) == 0 &&
           heap->mapped_size >= UTAMO_HEAP_MIN_BLOCK &&
           heap->mapped_size <= heap->max_size &&
           heap->max_size <= UINTPTR_MAX - (uintptr_t)heap->base &&
           heap->growth_size >= UTAMO_HEAP_MIN_BLOCK &&
           ((heap->mapped_size | heap->max_size | heap->growth_size) &
            UTAMO_HEAP_FLAG_MASK) == 0;
}

/* Called only after the physical block chain has been validated. */
static struct heap_block *exact_block(const struct heap_core *heap, size_t target)
{
    for (size_t offset = 0; offset < heap->mapped_size;) {
        struct heap_block *block = block_at(heap, offset);
        if (offset == target) {
            return block;
        }
        offset += block_size(block);
    }
    return NULL;
}

bool heap_core_validate(const struct heap_core *heap)
{
    if (!context_valid(heap)) {
        return false;
    }
    size_t previous = 0;
    size_t free_current = heap->free_head;
    size_t free_prior = UTAMO_HEAP_NONE;
    uint64_t live = 0;
    uint64_t requested = 0;
    bool previous_free = false;
    for (size_t offset = 0; offset < heap->mapped_size;) {
        if (heap->mapped_size - offset < sizeof(struct heap_block)) {
            return false;
        }
        const struct heap_block *block = block_at(heap, offset);
        const size_t size = block_size(block);
        if (block->cookie != block_cookie(heap, offset) ||
            (block->size_flags & (UTAMO_HEAP_FLAG_MASK & ~UTAMO_HEAP_USED)) != 0 ||
            size < UTAMO_HEAP_MIN_BLOCK || size > heap->mapped_size - offset ||
            block->prev_size != previous) {
            return false;
        }
        if (block_used(block)) {
            if (block->requested == 0 ||
                block->requested > size - sizeof(struct heap_block) ||
                block->free_prev != UTAMO_HEAP_NONE ||
                block->free_next != UTAMO_HEAP_NONE) {
                return false;
            }
            requested += (uint64_t)block->requested;
            ++live;
            previous_free = false;
        } else {
            if (block->requested != 0 || previous_free) {
                return false;
            }
            /* Free links are ordered by physical offset. Membership can be
             * verified during this walk without following an unchecked link. */
            if (free_current != offset || block->free_prev != free_prior) {
                return false;
            }
            free_prior = offset;
            free_current = block->free_next;
            previous_free = true;
        }
        previous = size;
        offset += size;
    }
    if (requested != heap->used_bytes || live != heap->live_allocations ||
        heap->peak_usage < requested) {
        return false;
    }
    return free_current == UTAMO_HEAP_NONE;
}

bool heap_core_init(struct heap_core *heap, void *base, size_t initial_size,
                    size_t max_size, size_t growth_size, bool debug_poison,
                    const struct heap_ops *ops)
{
    if (heap == NULL || heap->initialized || base == NULL ||
        ((uintptr_t)base & UTAMO_HEAP_FLAG_MASK) != 0 ||
        initial_size < UTAMO_HEAP_MIN_BLOCK || initial_size > max_size ||
        growth_size < UTAMO_HEAP_MIN_BLOCK ||
        ((initial_size | max_size | growth_size) & UTAMO_HEAP_FLAG_MASK) != 0 ||
        max_size > UINTPTR_MAX - (uintptr_t)base) {
        return false;
    }
    const struct heap_core initial = {
        .base = base, .mapped_size = initial_size, .max_size = max_size,
        .growth_size = growth_size, .free_head = 0,
        .ops = ops != NULL ? *ops : (struct heap_ops){0},
        .debug_poison = debug_poison, .initialized = true
    };
    *heap = initial;
    write_block(heap, 0, initial_size, 0, 0, false);
    if (debug_poison) {
        (void)memset(heap->base + sizeof(struct heap_block), 0xdd,
                     initial_size - sizeof(struct heap_block));
    }
    return true;
}

static void remove_free(struct heap_core *heap, size_t offset)
{
    struct heap_block *block = block_at(heap, offset);
    if (block->free_prev == UTAMO_HEAP_NONE) {
        heap->free_head = block->free_next;
    } else {
        block_at(heap, block->free_prev)->free_next = block->free_next;
    }
    if (block->free_next != UTAMO_HEAP_NONE) {
        block_at(heap, block->free_next)->free_prev = block->free_prev;
    }
    block->free_prev = UTAMO_HEAP_NONE;
    block->free_next = UTAMO_HEAP_NONE;
}

static void insert_free(struct heap_core *heap, size_t offset)
{
    size_t prior = UTAMO_HEAP_NONE;
    size_t current = heap->free_head;
    while (current != UTAMO_HEAP_NONE && current < offset) {
        prior = current;
        current = block_at(heap, current)->free_next;
    }
    struct heap_block *block = block_at(heap, offset);
    block->free_prev = prior;
    block->free_next = current;
    if (prior == UTAMO_HEAP_NONE) {
        heap->free_head = offset;
    } else {
        block_at(heap, prior)->free_next = offset;
    }
    if (current != UTAMO_HEAP_NONE) {
        block_at(heap, current)->free_prev = offset;
    }
}

static void following_previous(struct heap_core *heap, size_t offset, size_t size)
{
    if (offset + size < heap->mapped_size) {
        block_at(heap, offset + size)->prev_size = size;
    }
}

static void poison_free(struct heap_core *heap, size_t offset)
{
    if (heap->debug_poison) {
        (void)memset(heap->base + offset + sizeof(struct heap_block), 0xdd,
                     block_size(block_at(heap, offset)) - sizeof(struct heap_block));
    }
}

static size_t find_fit(const struct heap_core *heap, size_t needed)
{
    for (size_t offset = heap->free_head; offset != UTAMO_HEAP_NONE;
         offset = block_at(heap, offset)->free_next) {
        if (block_size(block_at(heap, offset)) >= needed) {
            return offset;
        }
    }
    return UTAMO_HEAP_NONE;
}

static size_t last_block(const struct heap_core *heap)
{
    size_t offset = 0;
    while (offset + block_size(block_at(heap, offset)) < heap->mapped_size) {
        offset += block_size(block_at(heap, offset));
    }
    return offset;
}

static bool grow_heap(struct heap_core *heap, size_t needed)
{
    if (heap->ops.grow == NULL || heap->mapped_size == heap->max_size) {
        return false;
    }
    if (needed < UTAMO_HEAP_MIN_BLOCK) {
        needed = UTAMO_HEAP_MIN_BLOCK;
    }
    const size_t tail_offset = last_block(heap);
    struct heap_block *tail = block_at(heap, tail_offset);
    const size_t tail_size = block_size(tail);
    const size_t tail_free = block_used(tail) ? 0 : tail_size;
    if (needed <= tail_free) {
        return true;
    }
    const size_t required = needed - tail_free;
    const size_t available = heap->max_size - heap->mapped_size;
    if (required > available) {
        return false;
    }
    const size_t units = required / heap->growth_size +
                         (required % heap->growth_size != 0 ? 1u : 0u);
    size_t extra = available;
    if (units <= available / heap->growth_size) {
        extra = units * heap->growth_size;
    }
    const size_t old_size = heap->mapped_size;
    const size_t new_size = old_size + extra;
    if (!heap->ops.grow(heap->ops.context, old_size, new_size)) {
        return false;
    }
    /* All fallible work preceded this point; append or extend one free block. */
    heap->mapped_size = new_size;
    if (!block_used(tail)) {
        remove_free(heap, tail_offset);
        write_block(heap, tail_offset, tail_size + extra, tail->prev_size, 0, false);
        insert_free(heap, tail_offset);
        poison_free(heap, tail_offset);
    } else {
        write_block(heap, old_size, extra, tail_size, 0, false);
        insert_free(heap, old_size);
        poison_free(heap, old_size);
    }
    return true;
}

static bool required_size(size_t requested, size_t *out)
{
    const size_t extra = sizeof(struct heap_block) + UTAMO_HEAP_FLAG_MASK;
    if (requested == 0 || requested > SIZE_MAX - extra) {
        return false;
    }
    *out = (requested + extra) & ~UTAMO_HEAP_FLAG_MASK;
    return true;
}

/* Keep requested bytes in an allocated block, returning a useful tail to the
 * free list. Its following block may already be free (e.g. realloc shrink). */
static void trim_block(struct heap_core *heap, size_t offset, size_t needed)
{
    struct heap_block *block = block_at(heap, offset);
    const size_t size = block_size(block);
    if (size - needed < UTAMO_HEAP_MIN_BLOCK) {
        return;
    }
    size_t remainder_size = size - needed;
    const size_t remainder_offset = offset + needed;
    const size_t old_next = offset + size;
    if (old_next < heap->mapped_size && !block_used(block_at(heap, old_next))) {
        remainder_size += block_size(block_at(heap, old_next));
        remove_free(heap, old_next);
    }
    block->size_flags = needed | UTAMO_HEAP_USED;
    write_block(heap, remainder_offset, remainder_size, needed, 0, false);
    following_previous(heap, remainder_offset, remainder_size);
    insert_free(heap, remainder_offset);
    poison_free(heap, remainder_offset);
}

static void *allocate(struct heap_core *heap, size_t requested, size_t needed,
                      bool allow_growth)
{
    size_t offset = find_fit(heap, needed);
    if (offset == UTAMO_HEAP_NONE && allow_growth && grow_heap(heap, needed)) {
        offset = find_fit(heap, needed);
    }
    if (offset == UTAMO_HEAP_NONE) {
        return NULL;
    }
    struct heap_block *block = block_at(heap, offset);
    remove_free(heap, offset);
    block->size_flags |= UTAMO_HEAP_USED;
    block->requested = requested;
    trim_block(heap, offset, needed);
    heap->used_bytes += (uint64_t)requested;
    ++heap->live_allocations;
    increment(&heap->allocations);
    if (heap->used_bytes > heap->peak_usage) {
        heap->peak_usage = heap->used_bytes;
    }
    void *pointer = heap->base + offset + sizeof(struct heap_block);
    if (heap->debug_poison) {
        (void)memset(pointer, 0xcd, block_size(block) - sizeof(struct heap_block));
    }
    return pointer;
}

static void failed_allocation(struct heap_core *heap)
{
    if (heap != NULL && heap->initialized) {
        increment(&heap->failed_allocations);
    }
}

void *heap_core_alloc(struct heap_core *heap, size_t size)
{
    if (size == 0) {
        return NULL;
    }
    size_t needed;
    if (!heap_core_validate(heap) || !required_size(size, &needed)) {
        failed_allocation(heap);
        return NULL;
    }
    void *pointer = allocate(heap, size, needed, true);
    if (pointer == NULL) {
        failed_allocation(heap);
    }
    return pointer;
}

void *heap_core_calloc(struct heap_core *heap, size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (count > SIZE_MAX / size) {
        failed_allocation(heap);
        return NULL;
    }
    const size_t bytes = count * size;
    void *pointer = heap_core_alloc(heap, bytes);
    if (pointer != NULL) {
        (void)memset(pointer, 0, bytes);
    }
    return pointer;
}

static size_t pointer_offset(const struct heap_core *heap, const void *pointer)
{
    const uintptr_t address = (uintptr_t)pointer;
    const uintptr_t base = (uintptr_t)heap->base;
    if (address < base || address - base < sizeof(struct heap_block) ||
        address - base >= heap->mapped_size ||
        (address & UTAMO_HEAP_FLAG_MASK) != 0) {
        return UTAMO_HEAP_NONE;
    }
    const size_t offset = (size_t)(address - base) - sizeof(struct heap_block);
    const struct heap_block *block = exact_block(heap, offset);
    return block != NULL && block_used(block) ? offset : UTAMO_HEAP_NONE;
}

static void release(struct heap_core *heap, size_t offset)
{
    struct heap_block *block = block_at(heap, offset);
    size_t size = block_size(block);
    size_t previous = block->prev_size;
    heap->used_bytes -= (uint64_t)block->requested;
    --heap->live_allocations;
    increment(&heap->frees);
    if (previous != 0 && !block_used(block_at(heap, offset - previous))) {
        const size_t previous_offset = offset - previous;
        struct heap_block *before = block_at(heap, previous_offset);
        remove_free(heap, previous_offset);
        size += previous;
        offset = previous_offset;
        previous = before->prev_size;
    }
    const size_t next = offset + size;
    if (next < heap->mapped_size && !block_used(block_at(heap, next))) {
        size += block_size(block_at(heap, next));
        remove_free(heap, next);
    }
    write_block(heap, offset, size, previous, 0, false);
    following_previous(heap, offset, size);
    insert_free(heap, offset);
    poison_free(heap, offset);
}

bool heap_core_free(struct heap_core *heap, void *pointer)
{
    if (pointer == NULL) {
        return true;
    }
    if (!heap_core_validate(heap)) {
        if (heap != NULL && heap->initialized) {
            increment(&heap->invalid_frees);
        }
        return false;
    }
    const size_t offset = pointer_offset(heap, pointer);
    if (offset == UTAMO_HEAP_NONE) {
        increment(&heap->invalid_frees);
        return false;
    }
    release(heap, offset);
    return true;
}

static void resize_block(struct heap_core *heap, size_t offset, size_t size,
                         size_t needed)
{
    struct heap_block *block = block_at(heap, offset);
    const size_t old_requested = block->requested;
    heap->used_bytes -= (uint64_t)old_requested;
    heap->used_bytes += (uint64_t)size;
    block->requested = size;
    trim_block(heap, offset, needed);
    if (heap->used_bytes > heap->peak_usage) {
        heap->peak_usage = heap->used_bytes;
    }
    if (heap->debug_poison) {
        unsigned char *payload = heap->base + offset + sizeof(struct heap_block);
        if (size > old_requested) {
            (void)memset(payload + old_requested, 0xcd, size - old_requested);
        }
        (void)memset(payload + size, 0xdd,
                     block_size(block) - sizeof(struct heap_block) - size);
    }
}

static bool consume_next(struct heap_core *heap, size_t offset, size_t needed)
{
    struct heap_block *block = block_at(heap, offset);
    const size_t current = block_size(block);
    const size_t next = offset + current;
    if (next == heap->mapped_size || block_used(block_at(heap, next)) ||
        block_size(block_at(heap, next)) < needed - current) {
        return false;
    }
    const size_t combined = current + block_size(block_at(heap, next));
    remove_free(heap, next);
    block->size_flags = combined | UTAMO_HEAP_USED;
    following_previous(heap, offset, combined);
    return true;
}

void *heap_core_realloc(struct heap_core *heap, void *pointer, size_t size)
{
    if (pointer == NULL) {
        return heap_core_alloc(heap, size);
    }
    if (size == 0) {
        (void)heap_core_free(heap, pointer);
        return NULL;
    }
    size_t needed;
    if (!heap_core_validate(heap) || !required_size(size, &needed)) {
        failed_allocation(heap);
        return NULL;
    }
    const size_t offset = pointer_offset(heap, pointer);
    if (offset == UTAMO_HEAP_NONE) {
        failed_allocation(heap);
        return NULL;
    }
    struct heap_block *block = block_at(heap, offset);
    const size_t current = block_size(block);
    if (needed <= current || consume_next(heap, offset, needed)) {
        resize_block(heap, offset, size, needed);
        return pointer;
    }
    /* Reuse an existing free block before asking for more mapped pages. */
    void *replacement = allocate(heap, size, needed, false);
    if (replacement == NULL) {
        const size_t next = offset + current;
        const bool at_tail = next == heap->mapped_size ||
            (!block_used(block_at(heap, next)) &&
             next + block_size(block_at(heap, next)) == heap->mapped_size);
        if (at_tail) {
            if (grow_heap(heap, needed - current) &&
                consume_next(heap, offset, needed)) {
                resize_block(heap, offset, size, needed);
                return pointer;
            }
        } else {
            replacement = allocate(heap, size, needed, true);
        }
    }
    if (replacement == NULL) {
        failed_allocation(heap);
        return NULL;
    }
    const size_t copy = block->requested < size ? block->requested : size;
    (void)memcpy(replacement, pointer, copy);
    release(heap, offset);
    return replacement;
}

bool heap_core_get_stats(const struct heap_core *heap, struct heap_stats *out)
{
    if (out == NULL || !heap_core_validate(heap)) {
        return false;
    }
    uint64_t free_bytes = 0;
    uint64_t largest = 0;
    for (size_t offset = heap->free_head; offset != UTAMO_HEAP_NONE;
         offset = block_at(heap, offset)->free_next) {
        const uint64_t bytes =
            (uint64_t)(block_size(block_at(heap, offset)) - sizeof(struct heap_block));
        free_bytes += bytes;
        if (bytes > largest) {
            largest = bytes;
        }
    }
    *out = (struct heap_stats){
        .mapped_bytes = (uint64_t)heap->mapped_size,
        .used_bytes = heap->used_bytes,
        .free_bytes = free_bytes,
        .overhead_bytes = (uint64_t)heap->mapped_size - heap->used_bytes - free_bytes,
        .live_allocations = heap->live_allocations,
        .allocations = heap->allocations,
        .frees = heap->frees,
        .peak_usage = heap->peak_usage,
        .failed_allocations = heap->failed_allocations,
        .invalid_frees = heap->invalid_frees,
        .largest_free_bytes = largest
    };
    return true;
}
