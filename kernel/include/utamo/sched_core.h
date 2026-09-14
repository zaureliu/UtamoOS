/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SCHED_CORE_H
#define UTAMO_SCHED_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTAMO_SCHED_MAX_TASKS 64u
enum sched_state {
    UTAMO_SCHED_RUNNING,
    UTAMO_SCHED_READY,
    UTAMO_SCHED_BLOCKED,
    UTAMO_SCHED_SLEEPING,
    UTAMO_SCHED_ZOMBIE
};

struct sched_core;
/* Embed in a caller-owned TCB. Nodes must be zero-initialized before init/add
 * and remain alive until remove_zombie succeeds. Never mutate core fields.
 * next is used by exactly one queue: READY FIFO or ordered sleepers.
 */
struct sched_task {
    struct sched_core *owner;
    uint64_t id;
    enum sched_state state;
    struct sched_task *next;
    uint64_t wake_tick;
    uint64_t run_ticks;
    uint32_t quantum_left;
    uint32_t preempt_depth;
};

struct sched_core {
    struct sched_task *tasks[UTAMO_SCHED_MAX_TASKS];
    struct sched_task *current;
    struct sched_task *idle;
    struct sched_task *ready_head;
    struct sched_task *ready_tail;
    struct sched_task *sleep_head;
    uint64_t now;
    uint64_t next_id;
    uint64_t ticks;
    uint64_t switches;
    size_t task_count;
    uint32_t quantum_ticks;
    bool reschedule;
    bool initialized;
};

struct sched_snapshot {
    uint64_t now, ticks, switches, current_id, next_id;
    size_t task_count;
    size_t ready_count; /* Actual FIFO members; standby idle is excluded. */
    size_t sleeping_count, blocked_count, zombie_count;
    uint32_t quantum_ticks;
    bool reschedule_pending; /* Raw request, even while preemption is disabled. */
};
struct sched_task_snapshot {
    uint64_t id, run_ticks, wake_tick;
    enum sched_state state;
    uint32_t quantum_left, preempt_depth;
    bool current, idle;
};

/* Pure single-CPU policy. Caller serializes every operation (normally IF=0).
 * No allocation, register frames, context switching, CPU or hardware calls.
 * idle id=0 and bootstrap id=1 count toward the bounded registry of 64.
 * core and both nodes start zeroed. init never runs a task.
 */
bool sched_core_init(struct sched_core *core, struct sched_task *idle,
                     struct sched_task *bootstrap, uint32_t quantum_ticks,
                     uint64_t now);
bool sched_core_add(struct sched_core *core, struct sched_task *task);
/* One call is one timer interrupt, even if absolute time is saturated.
 * Reject backward time; run/tick/switch counters saturate. Waking sleepers does
 * not switch tasks. Integration must select only after acknowledging the IRQ.
 */
bool sched_core_tick(struct sched_core *core, uint64_t now);
/* force is voluntary yield. Returns current while preemption is disabled;
 * returns NULL for invalid core. Selected task has a fresh quantum.
 */
struct sched_task *sched_core_select(struct sched_core *core, bool force);
/* Perform these transitions plus select within one caller critical section.
 * Idle and non-preemptible current tasks cannot sleep/block/exit.
 * delay=0 is rejected: callers implement sleep(0) as yield.
 */
bool sched_core_sleep_current(struct sched_core *core, uint64_t delay);
bool sched_core_block_current(struct sched_core *core);
bool sched_core_wake(struct sched_core *core, struct sched_task *task);
bool sched_core_exit_current(struct sched_core *core);
/* Only detached-from-execution zombies: never current or idle. Clears node
 * ownership so the caller may free/reuse its TCB after success. IDs never wrap.
 */
bool sched_core_remove_zombie(struct sched_core *core, struct sched_task *task);
bool sched_core_preempt_disable(struct sched_core *core);
bool sched_core_preempt_enable(struct sched_core *core);
/* Request is pending AND current preempt_depth is zero. No implicit switch. */
bool sched_core_reschedule_pending(const struct sched_core *core);
bool sched_core_validate(const struct sched_core *core);
bool sched_core_snapshot(const struct sched_core *core, struct sched_snapshot *out);
bool sched_core_task_snapshot(const struct sched_core *core,
                              const struct sched_task *task,
                              struct sched_task_snapshot *out);
const char *sched_state_name(enum sched_state state);
/* Nominal 100 Hz: ceil(ms/10), without adding 9 to a potentially maximal input. */
bool sched_ms_to_ticks(uint64_t milliseconds, uint64_t *out);

#endif
