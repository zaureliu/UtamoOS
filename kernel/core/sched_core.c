/* SPDX-License-Identifier: MIT */
#include <utamo/sched_core.h>

static void increment(uint64_t *value)
{
    if (*value != UINT64_MAX) {
        ++*value;
    }
}

static size_t task_slot(const struct sched_core *core, const struct sched_task *task)
{
    if (task != NULL) {
        for (size_t i = 0; i < UTAMO_SCHED_MAX_TASKS; ++i) {
            if (core->tasks[i] == task) {
                return i;
            }
        }
    }
    return UTAMO_SCHED_MAX_TASKS;
}

const char *sched_state_name(enum sched_state state)
{
    switch (state) {
    case UTAMO_SCHED_RUNNING: return "RUNNING";
    case UTAMO_SCHED_READY: return "READY";
    case UTAMO_SCHED_BLOCKED: return "BLOCKED";
    case UTAMO_SCHED_SLEEPING: return "SLEEPING";
    case UTAMO_SCHED_ZOMBIE: return "ZOMBIE";
    default: return "UNKNOWN";
    }
}

bool sched_ms_to_ticks(uint64_t milliseconds, uint64_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = milliseconds / 10u + (milliseconds % 10u != 0 ? 1u : 0u);
    return true;
}

bool sched_core_validate(const struct sched_core *core)
{
    if (core == NULL || !core->initialized || core->quantum_ticks == 0 ||
        core->task_count == 0 || core->task_count > UTAMO_SCHED_MAX_TASKS ||
        core->next_id < 2u ||
        task_slot(core, core->idle) == UTAMO_SCHED_MAX_TASKS ||
        task_slot(core, core->current) == UTAMO_SCHED_MAX_TASKS) {
        return false;
    }
    size_t count = 0;
    size_t ready_count = 0;
    size_t sleep_count = 0;
    size_t running_count = 0;
    for (size_t i = 0; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        const struct sched_task *task = core->tasks[i];
        if (task == NULL) {
            continue;
        }
        ++count;
        if (task->owner != core || task->id >= core->next_id ||
            (unsigned int)task->state > (unsigned int)UTAMO_SCHED_ZOMBIE ||
            task->quantum_left > core->quantum_ticks ||
            (task != core->current && task->preempt_depth != 0)) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (core->tasks[j] != NULL &&
                (core->tasks[j] == task || core->tasks[j]->id == task->id)) {
                return false;
            }
        }
        if (task == core->idle) {
            if (task->id != 0 || task->next != NULL || task->wake_tick != 0 ||
                task->state != (task == core->current ? UTAMO_SCHED_RUNNING :
                                UTAMO_SCHED_READY)) {
                return false;
            }
        } else if (task->id == 0) {
            return false;
        }
        if (task->state == UTAMO_SCHED_RUNNING) {
            if (task != core->current || task->next != NULL || task->wake_tick != 0) {
                return false;
            }
            ++running_count;
        } else if (task->state == UTAMO_SCHED_READY) {
            if (task->wake_tick != 0) {
                return false;
            }
            if (task != core->idle) {
                ++ready_count;
            }
        } else if (task->state == UTAMO_SCHED_SLEEPING) {
            if (task->wake_tick <= core->now || task->preempt_depth != 0) {
                return false;
            }
            ++sleep_count;
        } else if (task->next != NULL || task->wake_tick != 0 ||
                   task->preempt_depth != 0) {
            return false;
        }
    }
    if (count != core->task_count ||
        running_count != (core->current->state == UTAMO_SCHED_RUNNING ? 1u : 0u) ||
        (running_count == 0 && !core->reschedule)) {
        return false;
    }
    const struct sched_task *task = core->ready_head;
    const struct sched_task *last = NULL;
    for (size_t i = 0; i < ready_count; ++i) {
        if (task_slot(core, task) == UTAMO_SCHED_MAX_TASKS ||
            task == core->idle || task->state != UTAMO_SCHED_READY) {
            return false;
        }
        last = task;
        task = task->next;
    }
    if (task != NULL || last != core->ready_tail) {
        return false;
    }
    task = core->sleep_head;
    uint64_t previous_deadline = 0;
    for (size_t i = 0; i < sleep_count; ++i) {
        if (task_slot(core, task) == UTAMO_SCHED_MAX_TASKS ||
            task->state != UTAMO_SCHED_SLEEPING ||
            task->wake_tick < previous_deadline) {
            return false;
        }
        previous_deadline = task->wake_tick;
        task = task->next;
    }
    return task == NULL;
}

bool sched_core_init(struct sched_core *core, struct sched_task *idle,
                     struct sched_task *bootstrap, uint32_t quantum_ticks,
                     uint64_t now)
{
    if (core == NULL || core->initialized || idle == NULL || bootstrap == NULL ||
        idle == bootstrap || idle->owner != NULL || bootstrap->owner != NULL ||
        quantum_ticks == 0) {
        return false;
    }
    *core = (struct sched_core){
        .current = bootstrap, .idle = idle, .now = now, .next_id = 2,
        .task_count = 2, .quantum_ticks = quantum_ticks, .initialized = true
    };
    *idle = (struct sched_task){
        .owner = core, .id = 0, .state = UTAMO_SCHED_READY,
        .quantum_left = quantum_ticks
    };
    *bootstrap = (struct sched_task){
        .owner = core, .id = 1, .state = UTAMO_SCHED_RUNNING,
        .quantum_left = quantum_ticks
    };
    core->tasks[0] = idle;
    core->tasks[1] = bootstrap;
    return true;
}

static void ready_push(struct sched_core *core, struct sched_task *task)
{
    task->next = NULL;
    task->state = UTAMO_SCHED_READY;
    task->wake_tick = 0;
    if (core->ready_tail != NULL) {
        core->ready_tail->next = task;
    } else {
        core->ready_head = task;
    }
    core->ready_tail = task;
    if (core->current == core->idle || core->current->state != UTAMO_SCHED_RUNNING) {
        core->reschedule = true;
    }
}

bool sched_core_add(struct sched_core *core, struct sched_task *task)
{
    if (!sched_core_validate(core) || task == NULL || task->owner != NULL ||
        core->task_count == UTAMO_SCHED_MAX_TASKS || core->next_id == UINT64_MAX) {
        return false;
    }
    size_t slot = 0;
    while (core->tasks[slot] != NULL) {
        ++slot;
    }
    *task = (struct sched_task){
        .owner = core, .id = core->next_id, .state = UTAMO_SCHED_READY,
        .quantum_left = core->quantum_ticks
    };
    ++core->next_id;
    core->tasks[slot] = task;
    ++core->task_count;
    ready_push(core, task);
    return true;
}

bool sched_core_tick(struct sched_core *core, uint64_t now)
{
    if (!sched_core_validate(core) || now < core->now) {
        return false;
    }
    core->now = now;
    increment(&core->ticks);
    if (core->current->state == UTAMO_SCHED_RUNNING) {
        increment(&core->current->run_ticks);
        if (core->current != core->idle) {
            if (core->current->quantum_left != 0) {
                --core->current->quantum_left;
            }
            if (core->current->quantum_left == 0) {
                core->reschedule = true;
            }
        }
    }
    while (core->sleep_head != NULL && core->sleep_head->wake_tick <= now) {
        struct sched_task *task = core->sleep_head;
        core->sleep_head = task->next;
        ready_push(core, task);
    }
    return true;
}

struct sched_task *sched_core_select(struct sched_core *core, bool force)
{
    if (!sched_core_validate(core)) {
        return NULL;
    }
    struct sched_task *old = core->current;
    if (old->preempt_depth != 0 ||
        (!force && !core->reschedule && old->state == UTAMO_SCHED_RUNNING)) {
        return old;
    }
    if (old->state == UTAMO_SCHED_RUNNING) {
        if (old == core->idle) {
            old->state = UTAMO_SCHED_READY;
        } else {
            ready_push(core, old);
        }
    }
    struct sched_task *next = core->ready_head;
    if (next != NULL) {
        core->ready_head = next->next;
        if (core->ready_head == NULL) {
            core->ready_tail = NULL;
        }
    } else {
        next = core->idle;
    }
    next->next = NULL;
    next->state = UTAMO_SCHED_RUNNING;
    next->wake_tick = 0;
    next->quantum_left = core->quantum_ticks;
    core->current = next;
    core->reschedule = false;
    if (next != old) {
        increment(&core->switches);
    }
    return next;
}

static bool current_can_wait(const struct sched_core *core)
{
    return sched_core_validate(core) && core->current != core->idle &&
           core->current->state == UTAMO_SCHED_RUNNING &&
           core->current->preempt_depth == 0;
}

bool sched_core_sleep_current(struct sched_core *core, uint64_t delay)
{
    if (!current_can_wait(core) || delay == 0 || delay > UINT64_MAX - core->now) {
        return false;
    }
    struct sched_task *task = core->current;
    task->wake_tick = core->now + delay;
    task->state = UTAMO_SCHED_SLEEPING;
    struct sched_task **link = &core->sleep_head;
    while (*link != NULL && (*link)->wake_tick <= task->wake_tick) {
        link = &(*link)->next;
    }
    task->next = *link;
    *link = task;
    core->reschedule = true;
    return true;
}

bool sched_core_block_current(struct sched_core *core)
{
    if (!current_can_wait(core)) {
        return false;
    }
    core->current->state = UTAMO_SCHED_BLOCKED;
    core->reschedule = true;
    return true;
}

bool sched_core_wake(struct sched_core *core, struct sched_task *task)
{
    if (!sched_core_validate(core) ||
        task_slot(core, task) == UTAMO_SCHED_MAX_TASKS ||
        (task->state != UTAMO_SCHED_BLOCKED && task->state != UTAMO_SCHED_SLEEPING)) {
        return false;
    }
    if (task->state == UTAMO_SCHED_SLEEPING) {
        struct sched_task **link = &core->sleep_head;
        while (*link != task) {
            link = &(*link)->next;
        }
        *link = task->next;
    }
    ready_push(core, task);
    return true;
}

bool sched_core_exit_current(struct sched_core *core)
{
    if (!current_can_wait(core)) {
        return false;
    }
    core->current->state = UTAMO_SCHED_ZOMBIE;
    core->reschedule = true;
    return true;
}

bool sched_core_remove_zombie(struct sched_core *core, struct sched_task *task)
{
    if (!sched_core_validate(core)) {
        return false;
    }
    const size_t slot = task_slot(core, task);
    if (slot == UTAMO_SCHED_MAX_TASKS || task == core->current ||
        task == core->idle || task->state != UTAMO_SCHED_ZOMBIE) {
        return false;
    }
    core->tasks[slot] = NULL;
    --core->task_count;
    *task = (struct sched_task){0};
    return true;
}

bool sched_core_preempt_disable(struct sched_core *core)
{
    if (!sched_core_validate(core) || core->current->state != UTAMO_SCHED_RUNNING ||
        core->current->preempt_depth == UINT32_MAX) {
        return false;
    }
    ++core->current->preempt_depth;
    return true;
}

bool sched_core_preempt_enable(struct sched_core *core)
{
    if (!sched_core_validate(core) || core->current->state != UTAMO_SCHED_RUNNING ||
        core->current->preempt_depth == 0) {
        return false;
    }
    --core->current->preempt_depth;
    return true;
}

bool sched_core_reschedule_pending(const struct sched_core *core)
{
    return sched_core_validate(core) && core->reschedule &&
           core->current->preempt_depth == 0;
}

bool sched_core_snapshot(const struct sched_core *core, struct sched_snapshot *out)
{
    if (out == NULL || !sched_core_validate(core)) {
        return false;
    }
    struct sched_snapshot result = {
        .now = core->now, .ticks = core->ticks, .switches = core->switches,
        .current_id = core->current->id, .next_id = core->next_id,
        .task_count = core->task_count, .quantum_ticks = core->quantum_ticks,
        .reschedule_pending = core->reschedule
    };
    for (size_t i = 0; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        const struct sched_task *task = core->tasks[i];
        if (task == NULL || task == core->idle) {
            continue;
        }
        switch (task->state) {
        case UTAMO_SCHED_READY: ++result.ready_count; break;
        case UTAMO_SCHED_SLEEPING: ++result.sleeping_count; break;
        case UTAMO_SCHED_BLOCKED: ++result.blocked_count; break;
        case UTAMO_SCHED_ZOMBIE: ++result.zombie_count; break;
        case UTAMO_SCHED_RUNNING: break;
        default: return false;
        }
    }
    *out = result;
    return true;
}

bool sched_core_task_snapshot(const struct sched_core *core,
                              const struct sched_task *task,
                              struct sched_task_snapshot *out)
{
    if (out == NULL || !sched_core_validate(core) ||
        task_slot(core, task) == UTAMO_SCHED_MAX_TASKS) {
        return false;
    }
    *out = (struct sched_task_snapshot){
        .id = task->id, .run_ticks = task->run_ticks, .wake_tick = task->wake_tick,
        .state = task->state, .quantum_left = task->quantum_left,
        .preempt_depth = task->preempt_depth,
        .current = task == core->current, .idle = task == core->idle
    };
    return true;
}
