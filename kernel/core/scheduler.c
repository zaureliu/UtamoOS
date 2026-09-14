/* SPDX-License-Identifier: MIT */
#include <utamo/scheduler.h>
#include <utamo/cpu.h>
#include <utamo/heap.h>
#include <utamo/keyboard.h>
#include <utamo/panic.h>
#include <utamo/pit.h>
#include <utamo/string.h>
#include <utamo/thread_stack.h>
#include <utamo/memory.h>

struct kernel_thread {
    struct sched_task task; /* First: registry nodes convert without a lookup. */
    struct thread_stack stack;
    struct interrupt_frame *frame;
    thread_entry_fn entry;
    void *argument;
    char name[UTAMO_THREAD_NAME_SIZE];
    bool dynamic;
};
_Static_assert(offsetof(struct kernel_thread, task) == 0u, "embedded task");
extern unsigned char bootstrap_stack_bottom[];
extern unsigned char bootstrap_stack_top[];
static struct sched_core scheduler;
static struct kernel_thread bootstrap_thread;
static struct kernel_thread idle_thread;
static bool scheduler_ready;
static uint64_t timer_preemptions, threads_created, threads_exited, threads_reaped;

static void increment(uint64_t *value)
{
    if (*value != UINT64_MAX) {
        ++*value;
    }
}

static struct kernel_thread *as_thread(struct sched_task *task)
{
    return (struct kernel_thread *)(void *)task;
}

static bool valid_name(const char *name)
{
    if (name == NULL || *name == '\0') {
        return false;
    }
    for (size_t i = 0u; i < UTAMO_THREAD_NAME_SIZE; ++i) {
        if (name[i] == '\0') {
            return true;
        }
        if ((unsigned char)name[i] < 32u || (unsigned char)name[i] > 126u) {
            return false;
        }
    }
    return false;
}

static bool valid_frame(const struct kernel_thread *thread,
                        const struct interrupt_frame *frame)
{
    const uint64_t bottom = thread == &bootstrap_thread ?
        (uint64_t)(uintptr_t)bootstrap_stack_bottom : thread->stack.base;
    const uint64_t top = thread == &bootstrap_thread ?
        (uint64_t)(uintptr_t)bootstrap_stack_top : thread->stack.top;
    const uint64_t address = (uint64_t)(uintptr_t)frame;
    if (frame == NULL || bottom >= top || address < bottom ||
        address > top - sizeof(*frame) || (address & 7u) != 0u) {
        return false;
    }
    return frame->cs == 8u && frame->ss == 16u &&
           (frame->rflags & UINT64_C(2)) != 0u &&
           (frame->rflags & UINT64_C(0x7000)) == 0u &&
           frame->rsp >= bottom && frame->rsp <= top &&
           memory_is_canonical(frame->rip) &&
           frame->rip >= UINT64_C(0xffff800000000000);
}

static _Noreturn void thread_bootstrap(void)
{
    struct kernel_thread *const thread = as_thread(scheduler.current);
    if (thread == NULL || thread->entry == NULL) {
        PANIC("Thread bootstrap has no entry");
    }
    thread->entry(thread->argument);
    thread_exit();
}

static void idle_main(void *argument)
{
    (void)argument;
    for (;;) {
        thread_reap();
        cpu_disable_interrupts();
        cpu_wait_interrupt();
    }
}

bool scheduler_init(void)
{
    const uint64_t flags = cpu_irq_save();
    if (scheduler_ready || (flags & UINT64_C(0x200)) != 0u) {
        cpu_irq_restore(flags);
        return false;
    }
    if (!thread_stack_alloc(&idle_thread.stack)) {
        cpu_irq_restore(flags);
        return false;
    }
    if (!thread_frame_init(&idle_thread.stack,
            (uint64_t)(uintptr_t)thread_bootstrap,
            (uint64_t)(uintptr_t)cpu_halt, &idle_thread.frame)) {
        PANIC("Cannot construct idle interrupt frame");
    }
    idle_thread.entry = idle_main;
    memcpy(idle_thread.name, "idle", sizeof("idle"));
    memcpy(bootstrap_thread.name, "shell", sizeof("shell"));
    if (!sched_core_init(&scheduler, &idle_thread.task,
                         &bootstrap_thread.task, 2u, pit_get_ticks())) {
        PANIC("Cannot initialize scheduler queues");
    }
    scheduler_ready = true;
    cpu_irq_restore(flags);
    return true;
}

struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame)
{
    if (!scheduler_ready) {
        return frame;
    }
    struct kernel_thread *const old = as_thread(scheduler.current);
    if (!valid_frame(old, frame)) {
        PANIC("Invalid current thread interrupt frame");
    }
    old->frame = frame;
    if (frame->vector == 32u) {
        if (!sched_core_tick(&scheduler, pit_get_ticks())) {
            PANIC("Scheduler timer invariant");
        }
    } else if (frame->vector == 33u) {
        if (bootstrap_thread.task.state == UTAMO_SCHED_BLOCKED &&
            keyboard_has_pending() &&
            !sched_core_wake(&scheduler, &bootstrap_thread.task)) {
            PANIC("Cannot wake input thread");
        }
    } else if (frame->vector != UTAMO_SCHEDULE_VECTOR) {
        return frame;
    }
    struct sched_task *const selected = sched_core_select(
        &scheduler, frame->vector == UTAMO_SCHEDULE_VECTOR);
    if (selected == NULL) {
        PANIC("Scheduler selected no thread");
    }
    struct kernel_thread *const next = as_thread(selected);
    if (!valid_frame(next, next->frame)) {
        PANIC("Invalid selected thread interrupt frame");
    }
    if (old != next && old != &idle_thread && frame->vector == 32u) {
        increment(&timer_preemptions);
    }
    return next->frame;
}

bool thread_create(const char *name, thread_entry_fn entry, void *argument,
                   uint64_t *out_tid)
{
    const uint64_t flags = cpu_irq_save();
    if (!valid_name(name) || entry == NULL || out_tid == NULL) {
        cpu_irq_restore(flags);
        return false;
    }
    if (!scheduler_ready || (flags & UINT64_C(0x200)) == 0u ||
        scheduler.task_count == UTAMO_SCHED_MAX_TASKS) {
        cpu_irq_restore(flags);
        return false;
    }
    struct kernel_thread *const thread = kcalloc(1u, sizeof(*thread));
    if (thread == NULL) {
        cpu_irq_restore(flags);
        return false;
    }
    if (!thread_stack_alloc(&thread->stack)) {
        if (!kfree(thread)) {
            PANIC("Cannot roll back thread metadata");
        }
        cpu_irq_restore(flags);
        return false;
    }
    if (!thread_frame_init(&thread->stack,
            (uint64_t)(uintptr_t)thread_bootstrap,
            (uint64_t)(uintptr_t)cpu_halt, &thread->frame)) {
        PANIC("Cannot construct thread interrupt frame");
    }
    thread->entry = entry;
    thread->argument = argument;
    thread->dynamic = true;
    memcpy(thread->name, name, strlen(name) + 1u);
    if (!sched_core_add(&scheduler, &thread->task)) {
        if (!thread_stack_free(&thread->stack) || !kfree(thread)) {
            PANIC("Cannot roll back unqueued thread");
        }
        cpu_irq_restore(flags);
        return false;
    }
    *out_tid = thread->task.id;
    increment(&threads_created);
    cpu_irq_restore(flags);
    return true;
}

static bool can_block(uint64_t flags)
{
    return scheduler_ready && (flags & UINT64_C(0x200)) != 0u &&
           scheduler.current != scheduler.idle &&
           scheduler.current->preempt_depth == 0u;
}

bool thread_yield(void)
{
    const uint64_t flags = cpu_irq_save();
    if (!can_block(flags)) {
        cpu_irq_restore(flags);
        return false;
    }
    thread_yield_trap();
    cpu_irq_restore(flags);
    return true;
}

bool thread_sleep_ms(uint64_t milliseconds)
{
    if (milliseconds == 0u) {
        return thread_yield();
    }
    uint64_t delay;
    if (!sched_ms_to_ticks(milliseconds, &delay)) {
        return false;
    }
    const uint64_t flags = cpu_irq_save();
    if (!can_block(flags) || !sched_core_sleep_current(&scheduler, delay)) {
        cpu_irq_restore(flags);
        return false;
    }
    thread_yield_trap();
    cpu_irq_restore(flags);
    return true;
}

_Noreturn void thread_exit(void)
{
    const uint64_t flags = cpu_irq_save();
    if (!can_block(flags) || scheduler.current == &bootstrap_thread.task ||
        !sched_core_exit_current(&scheduler)) {
        PANIC("Invalid thread exit context");
    }
    increment(&threads_exited);
    thread_yield_trap();
    PANIC("Exited thread resumed");
}

void thread_reap(void)
{
    const uint64_t flags = cpu_irq_save();
    if (!scheduler_ready || (flags & UINT64_C(0x200)) == 0u) {
        cpu_irq_restore(flags);
        return;
    }
    /* IF stays off through detach/free: the old stack is never current here. */
    for (size_t i = 0u; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        struct sched_task *const task = scheduler.tasks[i];
        if (task == NULL || task == scheduler.current ||
            task->state != UTAMO_SCHED_ZOMBIE) {
            continue;
        }
        struct kernel_thread *const thread = as_thread(task);
        if (!thread->dynamic || !sched_core_remove_zombie(&scheduler, task) ||
            !thread_stack_free(&thread->stack) || !kfree(thread)) {
            PANIC("Thread reaper invariant");
        }
        increment(&threads_reaped);
    }
    cpu_irq_restore(flags);
}

void scheduler_wait_input(void)
{
    const uint64_t flags = cpu_irq_save();
    if (!can_block(flags) || scheduler.current != &bootstrap_thread.task) {
        cpu_irq_restore(flags);
        return;
    }
    if (!keyboard_has_pending()) {
        if (!sched_core_block_current(&scheduler)) {
            PANIC("Cannot block input thread");
        }
        thread_yield_trap();
    }
    cpu_irq_restore(flags);
}

void preempt_disable(void)
{
    const uint64_t flags = cpu_irq_save();
    if (scheduler_ready && !sched_core_preempt_disable(&scheduler)) {
        PANIC("Preemption nesting overflow");
    }
    cpu_irq_restore(flags);
}

void preempt_enable(void)
{
    const uint64_t flags = cpu_irq_save();
    if (scheduler_ready) {
        if (!sched_core_preempt_enable(&scheduler)) {
            PANIC("Preemption nesting underflow");
        }
        if ((flags & UINT64_C(0x200)) != 0u &&
            sched_core_reschedule_pending(&scheduler)) {
            thread_yield_trap();
        }
    }
    cpu_irq_restore(flags);
}

bool scheduler_get_stats(struct scheduler_stats *out)
{
    if (out == NULL) {
        return false;
    }
    const uint64_t flags = cpu_irq_save();
    struct scheduler_stats stats = {0};
    const bool valid = scheduler_ready &&
                       sched_core_snapshot(&scheduler, &stats.core);
    if (valid) {
        stats.timer_preemptions = timer_preemptions;
        stats.created = threads_created;
        stats.exited = threads_exited;
        stats.reaped = threads_reaped;
        *out = stats;
    }
    cpu_irq_restore(flags);
    return valid;
}

size_t scheduler_list(struct thread_snapshot *out, size_t capacity)
{
    if (out == NULL || capacity == 0u) {
        return 0u;
    }
    const uint64_t flags = cpu_irq_save();
    size_t count = 0u;
    if (scheduler_ready) {
        for (size_t i = 0u; i < UTAMO_SCHED_MAX_TASKS && count < capacity; ++i) {
            struct sched_task *const task = scheduler.tasks[i];
            if (task == NULL) {
                continue;
            }
            const struct kernel_thread *const thread = as_thread(task);
            struct thread_snapshot snapshot = {
                .id = task->id, .run_ticks = task->run_ticks,
                .wake_tick = task->wake_tick, .state = task->state,
                .stack_base = thread->stack.base, .stack_top = thread->stack.top,
                .guard = thread->stack.guard,
                .current = task == scheduler.current, .idle = task == scheduler.idle
            };
            memcpy(snapshot.name, thread->name, sizeof(snapshot.name));
            if (thread == &bootstrap_thread) {
                snapshot.stack_base = (uint64_t)(uintptr_t)bootstrap_stack_bottom;
                snapshot.stack_top = (uint64_t)(uintptr_t)bootstrap_stack_top;
            }
            out[count++] = snapshot;
        }
    }
    cpu_irq_restore(flags);
    return count;
}

bool scheduler_validate(void)
{
    const uint64_t flags = cpu_irq_save();
    const bool valid = scheduler_ready && sched_core_validate(&scheduler);
    cpu_irq_restore(flags);
    return valid;
}

_Noreturn void scheduler_fault_guard(void)
{
    cpu_disable_interrupts();
    if (!scheduler_ready || !idle_thread.stack.active) {
        PANIC("Stack guard probe unavailable");
    }
    *(volatile unsigned char *)(uintptr_t)idle_thread.stack.guard = 0u;
    PANIC("Stack guard probe unexpectedly returned");
}
