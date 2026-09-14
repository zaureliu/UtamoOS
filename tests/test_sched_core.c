/* SPDX-License-Identifier: MIT */
#include <utamo/sched_core.h>
#include <utamo/string.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

struct fixture {
    struct sched_core core;
    struct sched_task tasks[UTAMO_SCHED_MAX_TASKS];
};

static void start(struct fixture *fixture, uint64_t now)
{
    *fixture = (struct fixture){0};
    CHECK(sched_core_init(&fixture->core, &fixture->tasks[0],
                          &fixture->tasks[1], 2, now));
    CHECK(sched_core_validate(&fixture->core));
}

static void add(struct fixture *fixture, size_t index)
{
    CHECK(sched_core_add(&fixture->core, &fixture->tasks[index]));
    CHECK(sched_core_validate(&fixture->core));
}

static struct sched_snapshot snapshot(const struct sched_core *core)
{
    struct sched_snapshot out = {0};
    CHECK(sched_core_snapshot(core, &out));
    return out;
}

static void test_init_and_conversion(void)
{
    struct fixture f = {0};
    CHECK(!sched_core_init(NULL, &f.tasks[0], &f.tasks[1], 2, 0));
    CHECK(!sched_core_init(&f.core, NULL, &f.tasks[1], 2, 0));
    CHECK(!sched_core_init(&f.core, &f.tasks[0], NULL, 2, 0));
    CHECK(!sched_core_init(&f.core, &f.tasks[0], &f.tasks[0], 2, 0));
    CHECK(!sched_core_init(&f.core, &f.tasks[0], &f.tasks[1], 0, 0));
    CHECK(!f.core.initialized && f.tasks[0].owner == NULL);
    CHECK(!sched_core_validate(NULL));
    CHECK(!sched_core_validate(&f.core));
    CHECK(!sched_core_add(&f.core, &f.tasks[2]));
    CHECK(!sched_core_tick(&f.core, 0));
    CHECK(sched_core_select(&f.core, true) == NULL);
    start(&f, 100);
    CHECK(!sched_core_init(&f.core, &f.tasks[0], &f.tasks[1], 2, 100));
    const struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.now == 100 && view.current_id == 1 && view.next_id == 2);
    CHECK(view.task_count == 2 && view.ready_count == 0 && view.sleeping_count == 0);
    CHECK(view.ticks == 0 && view.switches == 0 && view.quantum_ticks == 2);
    CHECK(f.tasks[0].state == UTAMO_SCHED_READY && f.tasks[0].next == NULL);
    CHECK(f.tasks[1].state == UTAMO_SCHED_RUNNING);
    CHECK(!sched_core_tick(&f.core, 99));
    CHECK(f.core.now == 100 && f.core.ticks == 0);
    CHECK(strcmp(sched_state_name(UTAMO_SCHED_RUNNING), "RUNNING") == 0);
    CHECK(strcmp(sched_state_name(UTAMO_SCHED_READY), "READY") == 0);
    CHECK(strcmp(sched_state_name(UTAMO_SCHED_BLOCKED), "BLOCKED") == 0);
    CHECK(strcmp(sched_state_name(UTAMO_SCHED_SLEEPING), "SLEEPING") == 0);
    CHECK(strcmp(sched_state_name(UTAMO_SCHED_ZOMBIE), "ZOMBIE") == 0);
    CHECK(strcmp(sched_state_name((enum sched_state)99), "UNKNOWN") == 0);

    static const struct { uint64_t ms, ticks; } times[] = {
        {0, 0}, {1, 1}, {9, 1}, {10, 1}, {11, 2}, {19, 2}, {20, 2},
        {1000, 100}, {UINT64_MAX, UINT64_C(1844674407370955162)}
    };
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        uint64_t out = 999;
        CHECK(sched_ms_to_ticks(times[i].ms, &out));
        CHECK(out == times[i].ticks);
    }
    CHECK(!sched_ms_to_ticks(10, NULL));
}

static void test_fifo_quantum_and_yield(void)
{
    struct fixture f;
    start(&f, 0);
    add(&f, 2);
    add(&f, 3);
    CHECK(f.tasks[2].id == 2 && f.tasks[3].id == 3);
    CHECK(!sched_core_add(&f.core, &f.tasks[2]));
    CHECK(!sched_core_add(&f.core, NULL));
    CHECK(f.core.ready_head == &f.tasks[2] && f.core.ready_tail == &f.tasks[3]);
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(sched_core_tick(&f.core, 1));
    CHECK(f.tasks[1].quantum_left == 1 && f.tasks[1].run_ticks == 1);
    CHECK(!sched_core_reschedule_pending(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(sched_core_tick(&f.core, 2));
    CHECK(sched_core_reschedule_pending(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(f.tasks[2].quantum_left == 2 && f.core.switches == 1);
    CHECK(f.core.ready_head == &f.tasks[3] && f.core.ready_tail == &f.tasks[1]);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[3]);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[1]);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    const struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.ready_count == 2 && view.task_count == 4 && view.current_id == 2);
    CHECK(view.switches == 4 && !view.reschedule_pending);
    CHECK(sched_core_validate(&f.core));

    start(&f, 0);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[1]);
    CHECK(f.core.switches == 0 && f.core.ready_head == NULL);
    CHECK(sched_core_tick(&f.core, 1));
    CHECK(sched_core_tick(&f.core, 2));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(f.core.switches == 0 && f.tasks[1].quantum_left == 2);
}

static void test_block_wake_idle_and_reaping(void)
{
    struct fixture f;
    start(&f, 0);
    CHECK(!sched_core_wake(&f.core, &f.tasks[1]));
    CHECK(!sched_core_wake(&f.core, &f.tasks[2]));
    CHECK(sched_core_block_current(&f.core));
    CHECK(!sched_core_block_current(&f.core));
    CHECK(sched_core_validate(&f.core)); /* Transitional current, no RUNNING node. */
    CHECK(sched_core_select(&f.core, false) == &f.tasks[0]);
    CHECK(!sched_core_sleep_current(&f.core, 1));
    CHECK(!sched_core_block_current(&f.core));
    CHECK(!sched_core_exit_current(&f.core));
    CHECK(!sched_core_remove_zombie(&f.core, &f.tasks[0]));
    CHECK(sched_core_tick(&f.core, 1));
    CHECK(f.tasks[0].run_ticks == 1 && f.core.ready_head == NULL);
    CHECK(sched_core_select(&f.core, false) == &f.tasks[0]);
    add(&f, 2);
    CHECK(sched_core_reschedule_pending(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(sched_core_exit_current(&f.core));
    CHECK(!sched_core_remove_zombie(&f.core, &f.tasks[2])); /* Still executing stack. */
    struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.zombie_count == 1 && view.blocked_count == 1);
    CHECK(sched_core_select(&f.core, false) == &f.tasks[0]);
    CHECK(!sched_core_wake(&f.core, &f.tasks[2]));
    CHECK(sched_core_remove_zombie(&f.core, &f.tasks[2]));
    CHECK(f.tasks[2].owner == NULL && f.core.task_count == 2);
    CHECK(!sched_core_remove_zombie(&f.core, &f.tasks[2]));
    CHECK(sched_core_wake(&f.core, &f.tasks[1]));
    CHECK(!sched_core_wake(&f.core, &f.tasks[1]));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    add(&f, 2);
    CHECK(f.tasks[2].id == 3); /* Removed slot reused, identifier never reused. */
    CHECK(sched_core_exit_current(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(sched_core_remove_zombie(&f.core, &f.tasks[1]));
    CHECK(sched_core_validate(&f.core));
    view = snapshot(&f.core);
    CHECK(view.task_count == 2 && view.zombie_count == 0 && view.blocked_count == 0);
}

static void prepare_sleepers(struct fixture *f)
{
    start(f, 0);
    add(f, 2);
    add(f, 3);
    add(f, 4);
    CHECK(sched_core_sleep_current(&f->core, 10));
    CHECK(sched_core_select(&f->core, true) == &f->tasks[2]);
    CHECK(sched_core_sleep_current(&f->core, 10));
    CHECK(sched_core_select(&f->core, true) == &f->tasks[3]);
    CHECK(sched_core_sleep_current(&f->core, 5));
    CHECK(sched_core_select(&f->core, true) == &f->tasks[4]);
    CHECK(sched_core_sleep_current(&f->core, 10));
    CHECK(sched_core_select(&f->core, true) == &f->tasks[0]);
    CHECK(f->core.sleep_head == &f->tasks[3]);
    CHECK(f->tasks[3].next == &f->tasks[1] &&
          f->tasks[1].next == &f->tasks[2] &&
          f->tasks[2].next == &f->tasks[4]);
}

static void test_sleep_deadlines_and_cancel(void)
{
    struct fixture f;
    prepare_sleepers(&f);
    struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.sleeping_count == 4 && view.ready_count == 0);
    CHECK(sched_core_tick(&f.core, 4));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[0]);
    CHECK(sched_core_tick(&f.core, 5));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[3]);
    CHECK(f.tasks[3].wake_tick == 0);
    CHECK(sched_core_block_current(&f.core));
    CHECK(sched_core_select(&f.core, true) == &f.tasks[0]);
    CHECK(sched_core_tick(&f.core, 10));
    CHECK(f.core.ready_head == &f.tasks[1] &&
          f.tasks[1].next == &f.tasks[2] &&
          f.tasks[2].next == &f.tasks[4]);
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[4]);
    view = snapshot(&f.core);
    CHECK(view.sleeping_count == 0 && view.ready_count == 2 && view.blocked_count == 1);

    prepare_sleepers(&f);
    CHECK(sched_core_wake(&f.core, &f.tasks[2])); /* Middle of sleep list. */
    CHECK(sched_core_wake(&f.core, &f.tasks[4])); /* Tail. */
    CHECK(sched_core_wake(&f.core, &f.tasks[3])); /* Head. */
    CHECK(f.core.sleep_head == &f.tasks[1] && f.tasks[1].next == NULL);
    CHECK(f.core.ready_head == &f.tasks[2] &&
          f.tasks[2].next == &f.tasks[4] && f.tasks[4].next == &f.tasks[3]);
    CHECK(!sched_core_wake(&f.core, &f.tasks[2]));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(!sched_core_sleep_current(&f.core, 0));
    CHECK(sched_core_validate(&f.core));
}

static void test_preempt_nesting_and_time_limits(void)
{
    struct fixture f;
    start(&f, 0);
    add(&f, 2);
    CHECK(sched_core_sleep_current(&f.core, 10));
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    CHECK(sched_core_preempt_disable(&f.core));
    CHECK(sched_core_preempt_disable(&f.core));
    CHECK(!sched_core_sleep_current(&f.core, 1));
    CHECK(!sched_core_block_current(&f.core));
    CHECK(!sched_core_exit_current(&f.core));
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    CHECK(sched_core_tick(&f.core, 9));
    CHECK(sched_core_tick(&f.core, 10));
    CHECK(f.core.ready_head == &f.tasks[1] && f.core.reschedule);
    CHECK(!sched_core_reschedule_pending(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(sched_core_preempt_enable(&f.core));
    CHECK(f.tasks[2].preempt_depth == 1);
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    CHECK(sched_core_preempt_enable(&f.core));
    CHECK(sched_core_reschedule_pending(&f.core));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(!sched_core_preempt_enable(&f.core));
    f.tasks[1].preempt_depth = UINT32_MAX;
    CHECK(!sched_core_preempt_disable(&f.core));
    CHECK(f.tasks[1].preempt_depth == UINT32_MAX);
    f.tasks[1].preempt_depth = 0;
    CHECK(!sched_core_sleep_current(&f.core, UINT64_MAX));
    CHECK(f.tasks[1].state == UTAMO_SCHED_RUNNING);

    start(&f, UINT64_MAX - 1u);
    add(&f, 2);
    CHECK(sched_core_sleep_current(&f.core, 1));
    CHECK(sched_core_select(&f.core, true) == &f.tasks[2]);
    f.core.ticks = UINT64_MAX;
    f.core.switches = UINT64_MAX;
    f.tasks[2].run_ticks = UINT64_MAX;
    CHECK(sched_core_tick(&f.core, UINT64_MAX));
    CHECK(f.core.ready_head == &f.tasks[1]);
    CHECK(sched_core_tick(&f.core, UINT64_MAX)); /* Saturated time still preempts. */
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(f.core.ticks == UINT64_MAX && f.core.switches == UINT64_MAX);
    CHECK(f.tasks[2].run_ticks == UINT64_MAX);
    CHECK(!sched_core_sleep_current(&f.core, 1));
    CHECK(sched_core_tick(&f.core, UINT64_MAX));
    CHECK(sched_core_tick(&f.core, UINT64_MAX));
    CHECK(sched_core_select(&f.core, false) == &f.tasks[2]);
    CHECK(sched_core_validate(&f.core));
}

static void test_registry_limits_and_snapshots(void)
{
    struct fixture f;
    start(&f, 0);
    for (size_t i = 2; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        add(&f, i);
        CHECK(f.tasks[i].id == (uint64_t)i);
    }
    struct sched_task extra = {0};
    CHECK(!sched_core_add(&f.core, &extra));
    CHECK(extra.owner == NULL && f.core.next_id == 64);
    struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.task_count == 64 && view.ready_count == 62);
    struct sched_task_snapshot task;
    CHECK(sched_core_task_snapshot(&f.core, &f.tasks[1], &task));
    CHECK(task.current && !task.idle && task.state == UTAMO_SCHED_RUNNING);
    CHECK(sched_core_task_snapshot(&f.core, &f.tasks[0], &task));
    CHECK(task.idle && !task.current && task.id == 0);
    task.id = 123;
    CHECK(!sched_core_task_snapshot(&f.core, &extra, &task));
    CHECK(task.id == 123);
    CHECK(!sched_core_task_snapshot(&f.core, NULL, &task));
    CHECK(!sched_core_task_snapshot(&f.core, &f.tasks[0], NULL));
    CHECK(!sched_core_snapshot(&f.core, NULL));
    view.current_id = 123;
    CHECK(!sched_core_snapshot(NULL, &view));
    CHECK(view.current_id == 123);

    start(&f, 0);
    f.core.next_id = UINT64_MAX - 1u;
    add(&f, 2);
    CHECK(f.tasks[2].id == UINT64_MAX - 1u && f.core.next_id == UINT64_MAX);
    CHECK(!sched_core_add(&f.core, &f.tasks[3]));
    CHECK(f.tasks[3].owner == NULL && f.core.next_id == UINT64_MAX);
    CHECK(sched_core_validate(&f.core));

    struct fixture other;
    start(&other, 0);
    CHECK(!sched_core_add(&other.core, &f.tasks[2]));
    CHECK(!sched_core_wake(&other.core, &f.tasks[2]));
    CHECK(!sched_core_remove_zombie(&other.core, &f.tasks[2]));
}

static void test_invalid_queue_metadata(void)
{
    struct fixture f;
    start(&f, 0);
    add(&f, 2);
    f.tasks[2].next = &f.tasks[2];
    CHECK(!sched_core_validate(&f.core));
    CHECK(sched_core_select(&f.core, true) == NULL);
    CHECK(!sched_core_tick(&f.core, 1));
    CHECK(f.core.now == 0 && f.core.current == &f.tasks[1]);
    f.tasks[2].next = NULL;
    CHECK(sched_core_validate(&f.core));
    f.core.ready_head = (struct sched_task *)(uintptr_t)UINT64_C(0x1230);
    CHECK(!sched_core_validate(&f.core)); /* Not dereferenced before membership. */
    f.core.ready_head = &f.tasks[2];
    f.core.ready_tail = &f.tasks[1];
    CHECK(!sched_core_validate(&f.core));
    f.core.ready_tail = &f.tasks[2];
    f.tasks[2].state = (enum sched_state)99;
    CHECK(!sched_core_validate(&f.core));
    f.tasks[2].state = UTAMO_SCHED_READY;
    f.tasks[2].id = 1;
    CHECK(!sched_core_validate(&f.core));
    f.tasks[2].id = 2;
    CHECK(sched_core_validate(&f.core));
    f.core.tasks[3] = &f.tasks[2];
    ++f.core.task_count;
    CHECK(!sched_core_validate(&f.core));

    prepare_sleepers(&f);
    f.tasks[4].next = f.core.sleep_head;
    CHECK(!sched_core_validate(&f.core));
    f.tasks[4].next = NULL;
    f.tasks[2].wake_tick = 4; /* Deadline out of order and before preceding node. */
    CHECK(!sched_core_validate(&f.core));
    f.tasks[2].wake_tick = 10;
    CHECK(sched_core_validate(&f.core));
}

static void test_bounded_round_robin_progress(void)
{
    struct fixture f;
    start(&f, 0);
    for (size_t i = 2; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        add(&f, i);
    }
    const uint64_t rounds = 3;
    const uint64_t ticks = (UTAMO_SCHED_MAX_TASKS - 1u) * 2u * rounds;
    for (uint64_t tick = 1; tick <= ticks; ++tick) {
        const uint64_t expected_before = ((tick - 1u) / 2u) % 63u + 1u;
        CHECK(f.core.current->id == expected_before);
        CHECK(sched_core_tick(&f.core, tick));
        CHECK(sched_core_select(&f.core, false) != NULL);
        CHECK(sched_core_validate(&f.core));
    }
    for (size_t i = 1; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        CHECK(f.tasks[i].run_ticks == 2u * rounds);
    }
    CHECK(f.tasks[0].run_ticks == 0 && f.core.current == &f.tasks[1]);
    struct sched_snapshot view = snapshot(&f.core);
    CHECK(view.ticks == ticks && view.switches == ticks / 2u);
    CHECK(view.ready_count == 62 && view.task_count == 64);

    /* Repeated lifecycle operations free registry capacity without reusing IDs. */
    for (size_t i = 1; i < UTAMO_SCHED_MAX_TASKS; ++i) {
        struct sched_task *departing = f.core.current;
        CHECK(departing != f.core.idle);
        CHECK(sched_core_exit_current(&f.core));
        CHECK(!sched_core_remove_zombie(&f.core, departing));
        CHECK(sched_core_select(&f.core, true) != NULL);
        CHECK(sched_core_remove_zombie(&f.core, departing));
        CHECK(sched_core_validate(&f.core));
    }
    CHECK(f.core.current == f.core.idle && f.core.task_count == 1);
    add(&f, 1);
    CHECK(f.tasks[1].id == 64);
    CHECK(sched_core_select(&f.core, false) == &f.tasks[1]);
    CHECK(sched_core_validate(&f.core));
}

int main(void)
{
    test_init_and_conversion();
    test_fifo_quantum_and_yield();
    test_block_wake_idle_and_reaping();
    test_sleep_deadlines_and_cancel();
    test_preempt_nesting_and_time_limits();
    test_registry_limits_and_snapshots();
    test_invalid_queue_metadata();
    test_bounded_round_robin_progress();
    (void)printf("UTAMO scheduler core host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0 ? 0 : 1;
}
