/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_HEAP_CORE_H
#define UTAMO_HEAP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTAMO_HEAP_ALIGNMENT 16u

struct heap_ops {
    void *context;
    /* Append an accessible contiguous prefix. Failure leaves mappings and
     * data-frame ownership unchanged. Never calls back into this heap.
     */
    bool (*grow)(void *context, size_t old_mapped, size_t new_mapped);
};

struct heap_stats {
    uint64_t mapped_bytes;
    uint64_t used_bytes;       /* Exact bytes requested by live allocations. */
    uint64_t free_bytes;       /* Payload capacity in free blocks. */
    uint64_t overhead_bytes;   /* Headers plus internal alignment/slack. */
    uint64_t live_allocations;
    uint64_t allocations;
    uint64_t frees;
    uint64_t peak_usage;       /* Peak requested bytes, not mapped capacity. */
    uint64_t failed_allocations;
    uint64_t invalid_frees;
    uint64_t largest_free_bytes;
};

/* Initialize to zero before init. Fields are private to heap_core.c; exposed
 * only so callers can supply static storage. No hardware or synchronization
 * is embedded: callers serialize all operations, including stats/validation.
 */
struct heap_core {
    unsigned char *base;
    size_t mapped_size;
    size_t max_size;
    size_t growth_size;
    size_t free_head;
    struct heap_ops ops;
    uint64_t used_bytes;
    uint64_t live_allocations;
    uint64_t allocations;
    uint64_t frees;
    uint64_t peak_usage;
    uint64_t failed_allocations;
    uint64_t invalid_frees;
    bool debug_poison;
    bool initialized;
};

/* initial_size is already accessible; init never calls grow. Sizes and base
 * are 16-byte aligned; initial/growth sizes are at least 64 bytes. The reserved
 * max_size range is contiguous and owned by this heap. NULL ops disables growth.
 * Failure preserves the state and arena. Successful init cannot be repeated.
 */
bool heap_core_init(struct heap_core *heap, void *base, size_t initial_size,
                    size_t max_size, size_t growth_size, bool debug_poison,
                    const struct heap_ops *ops);
/* alloc(0), calloc with a zero factor return NULL without counting failure.
 * Successful allocations have 16-byte alignment. calloc checks multiplication.
 * Debug poison uses CD for new payload and DD for free/unused payload.
 */
void *heap_core_alloc(struct heap_core *heap, size_t size);
void *heap_core_calloc(struct heap_core *heap, size_t count, size_t size);
/* NULL is a no-op success. Foreign/interior/already-free pointers are rejected
 * without dereferencing caller-controlled header addresses. Stale pointers
 * cannot be distinguished after the same address is legitimately reallocated.
 */
bool heap_core_free(struct heap_core *heap, void *pointer);
/* NULL delegates to alloc; zero size frees and returns NULL. Failure preserves
 * the original allocation. In-place resize does not add an allocation/free;
 * moving resize counts both. Invalid nonzero realloc counts an allocation
 * failure; invalid_frees counts only rejected free operations.
 */
void *heap_core_realloc(struct heap_core *heap, void *pointer, size_t size);
bool heap_core_validate(const struct heap_core *heap);
bool heap_core_get_stats(const struct heap_core *heap, struct heap_stats *out);

#endif
