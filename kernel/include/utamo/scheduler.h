/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SCHEDULER_H
#define UTAMO_SCHEDULER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/interrupts.h>
#include <utamo/sched_core.h>

#define UTAMO_SCHEDULE_VECTOR 240u
#define UTAMO_THREAD_NAME_SIZE 24u
typedef void (*thread_entry_fn)(void *argument);
struct scheduler_stats {
    struct sched_snapshot core;
    uint64_t timer_preemptions, created, exited, reaped;
    uint64_t user_timer_preemptions, address_space_switches;
};
struct thread_snapshot {
    uint64_t id, run_ticks, wake_tick, stack_base, stack_top, guard;
    enum sched_state state;
    char name[UTAMO_THREAD_NAME_SIZE];
    bool current, idle;
};
/* BSP only. Init once after heap/PIT setup, before STI. */
bool scheduler_init(void);
/* Called only at the outer IRQ0/IRQ1 epilogue AFTER EOI, or DPL0 INT240.
 * IF=0. Never allocates or reaps. Returns the frame to restore with IRETQ. */
struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame);
struct process;
/* Internal process publisher, IF=0; address space already complete and owned. */
bool scheduler_create_user(const char *name, struct process *owner,
                           uint64_t entry, uint64_t rsp, uint64_t argument,
                           uint64_t *out_tid);
struct process *scheduler_current_process(void);
/* Frame object must belong to current protected kernel stack. Invalid user
 * return values are a process error; damaged kernel frame ownership is fatal. */
bool scheduler_user_frame_safe(const struct interrupt_frame *frame);
struct interrupt_frame *scheduler_resume_user(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_yield_user(struct interrupt_frame *frame,
                                           uint64_t milliseconds);
struct interrupt_frame *scheduler_exit_user(struct interrupt_frame *frame);
/* Thread context, IF=1. No allocation/sleep/yield/exit from IRQ/NMI.
 * New threads share CR3, FS/GS and use general registers only. */
bool thread_create(const char *name, thread_entry_fn entry, void *argument,
                   uint64_t *out_tid);
bool thread_yield(void);
bool thread_sleep_ms(uint64_t milliseconds);
_Noreturn void thread_exit(void);
void thread_reap(void);
/* Exclusive PS/2 consumer: bootstrap shell. Atomic pending-check/block. */
void scheduler_wait_input(void);
/* Nesting per thread; no-ops before initialization. IRQs remain enabled.
 * Never explicitly sleep/yield/exit inside a nonpreemptible region. */
void preempt_disable(void);
void preempt_enable(void);
bool scheduler_get_stats(struct scheduler_stats *out);
size_t scheduler_list(struct thread_snapshot *out, size_t capacity);
bool scheduler_validate(void);
bool scheduler_selftest(void);
/* Explicit fatal probe only: write to the idle stack guard. */
_Noreturn void scheduler_fault_guard(void);
/* Assembly INT240 helper. Internal callers hold IF=0 transactionally. */
void thread_yield_trap(void);
#endif
