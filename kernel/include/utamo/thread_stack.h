/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_THREAD_STACK_H
#define UTAMO_THREAD_STACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/interrupts.h>
#include <utamo/vmm.h>

#define UTAMO_THREAD_STACK_REGION (VMM_DYNAMIC_BASE + UINT64_C(0x40000000))
#define UTAMO_THREAD_STACK_SIZE ((size_t)65536u)
#define UTAMO_THREAD_STACK_SLOTS 64u
#define UTAMO_THREAD_STACK_STRIDE UINT64_C(69632)

struct thread_stack {
    uint64_t guard;
    uint64_t base;
    uint64_t top;
    size_t slot;
    uint64_t generation;
    bool active;
};

/* BSP thread context only. No heap allocation. Invalid/OOM leaves *out unchanged.
 * A successful allocation exclusively owns sixteen PMM frames; its lower guard
 * remains unmapped. The caller supplies an output object not already owning a
 * stack; it need not initialize this output object beforehand.
 */
bool thread_stack_alloc(struct thread_stack *out);
/* Only from another stack, never an IRQ/NMI. Invalid/stale descriptors return
 * false without mutations. Unexpected cleanup failure is fatal. Empty VMM
 * intermediate tables remain pinned. Successful free zeroes the descriptor.
 */
bool thread_stack_free(struct thread_stack *stack);
/* Construct a supervisor frame in the final mapped page using its HHDM alias.
 * entry/sentinel must be nonzero canonical instruction addresses. Sentinel is
 * an assembly halt/trap entry, not a normal C entry: RET would leave RSP%16=0.
 * On success *out is the frame's stack virtual address, not its HHDM alias.
 */
bool thread_frame_init(const struct thread_stack *stack, uint64_t entry,
                       uint64_t return_sentinel, struct interrupt_frame **out);

#endif
