/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_HEAP_H
#define UTAMO_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/heap_core.h>

/* Dedicated subrange of the VMM arena, beyond the 4-MiB memory test range. */
#define UTAMO_HEAP_BASE UINT64_C(0xffffc00001000000)
#define UTAMO_HEAP_INITIAL_SIZE ((size_t)65536u)
#define UTAMO_HEAP_GROWTH_SIZE ((size_t)65536u)
#define UTAMO_HEAP_MAX_SIZE ((size_t)67108864u)

/* BSP thread/boot context only; IRQ/NMI callers must not allocate. */
bool heap_init(void);
void *kmalloc(size_t bytes);
void *kcalloc(size_t count, size_t bytes);
/* Failure preserves the original allocation; realloc(p, 0) frees it. */
void *krealloc(void *pointer, size_t bytes);
/* NULL succeeds; invalid/interior/already-freed pointers return false. */
bool kfree(void *pointer);
bool heap_get_stats(struct heap_stats *stats);
bool heap_validate(void);
/* Explicit, deterministic and bounded. Never runs during normal boot. */
bool heap_selftest(void);

#endif
