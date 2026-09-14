/* SPDX-License-Identifier: MIT */
#include <utamo/scheduler.h>
#include <utamo/cpu.h>
#include <utamo/heap.h>
#include <utamo/log.h>
#include <utamo/memory.h>
#include <utamo/panic.h>
#include <utamo/pit.h>
#include <utamo/pmm.h>
#include <utamo/vmm.h>
#include <utamo/string.h>

bool thread_register_probe(void);
#define WORKERS 4u
struct test_worker {
    volatile uint64_t operations;
    volatile bool done, failed;
    unsigned int mode, loops;
};

static bool pattern_ok(const unsigned char *data, size_t size, unsigned char value)
{
    for (size_t i = 0u; i < size; ++i) {
        if (data[i] != (unsigned char)(value ^ (unsigned char)i)) {
            return false;
        }
    }
    return true;
}

static void pattern_set(unsigned char *data, size_t size, unsigned char value)
{
    for (size_t i = 0u; i < size; ++i) {
        data[i] = (unsigned char)(value ^ (unsigned char)i);
    }
}

static void worker_main(void *argument)
{
    struct test_worker *const worker = argument;
    volatile uint64_t stack_pattern[32];
    for (size_t i = 0u; i < 32u; ++i) {
        stack_pattern[i] = UINT64_C(0x4153545241535441) ^ (uint64_t)i;
    }
    if (worker->mode == 1u) {
        /* No yield/sleep: progress by other threads requires IRQ preemption. */
        const uint64_t deadline = pit_get_ticks() + 24u;
        while (pit_get_ticks() < deadline) {
            ++worker->operations;
        }
    } else {
        for (unsigned int i = 0u; i < worker->loops; ++i) {
            const size_t size = 1u + ((size_t)i * 73u) % 2048u;
            const unsigned char marker = (unsigned char)(i ^ worker->mode);
            unsigned char *data = kmalloc(size);
            if (data == NULL) {
                worker->failed = true;
                break;
            }
            pattern_set(data, size, marker);
            if (!thread_register_probe() || !thread_yield() ||
                !pattern_ok(data, size, marker)) {
                worker->failed = true;
            }
            unsigned char *const grown = krealloc(data, size + 37u);
            if (grown == NULL) {
                worker->failed = true;
            } else {
                data = grown;
                if (!pattern_ok(data, size, marker)) {
                    worker->failed = true;
                }
            }
            if (!kfree(data)) {
                worker->failed = true;
            }
            if (worker->mode == 2u) {
                const uint64_t before = pit_get_ticks();
                if (!thread_sleep_ms(20u) || pit_get_ticks() - before < 2u) {
                    worker->failed = true;
                }
            }
            for (size_t j = 0u; j < 32u; ++j) {
                if (stack_pattern[j] !=
                    (UINT64_C(0x4153545241535441) ^ (uint64_t)j)) {
                    worker->failed = true;
                }
            }
            ++worker->operations;
        }
    }
    worker->done = true;
    /* Return exercises trampoline -> exit -> deferred stack/TCB cleanup. */
}

static bool run_workers(unsigned int mode, unsigned int loops, uint64_t *operations,
                        uint64_t *cpu_iterations)
{
    struct test_worker workers[WORKERS] = {0};
    size_t started = 0u;
    bool good = true;
    for (size_t i = 0u; i < WORKERS; ++i) {
        workers[i].mode = mode;
        workers[i].loops = loops;
        uint64_t tid;
        if (!thread_create("astra-worker", worker_main, &workers[i], &tid)) {
            good = false;
            break;
        }
        ++started;
    }
    const uint64_t limit = pit_get_ticks() + 1000u;
    for (;;) {
        size_t done = 0u;
        for (size_t i = 0u; i < started; ++i) {
            if (workers[i].done) {
                ++done;
            }
        }
        if (done == started) {
            break;
        }
        if (pit_get_ticks() >= limit) {
            /* Arguments live on this stack: never return while a worker uses it. */
            PANIC("Scheduler self-test workers did not finish");
        }
        if (!thread_sleep_ms(10u)) {
            PANIC("Scheduler self-test cannot wait");
        }
        thread_reap();
    }
    /* done is published before trampoline exit; let every worker leave first. */
    for (;;) {
        struct scheduler_stats stats;
        if (!scheduler_get_stats(&stats)) {
            PANIC("Scheduler self-test cannot inspect cleanup");
        }
        thread_reap();
        if (stats.core.task_count == 2u) {
            break;
        }
        if (pit_get_ticks() >= limit || !thread_sleep_ms(10u)) {
            PANIC("Scheduler self-test cleanup did not finish");
        }
    }
    for (size_t i = 0u; i < started; ++i) {
        good = good && !workers[i].failed && workers[i].operations != 0u &&
               (mode == 1u || workers[i].operations == loops);
        if (mode == 1u) {
            *cpu_iterations += workers[i].operations;
        } else {
            *operations += workers[i].operations;
        }
    }
    return good && started == WORKERS;
}

bool scheduler_selftest(void)
{
    struct scheduler_stats before, after;
    struct heap_stats heap_before, heap_after;
    struct pmm_stats pmm_before, pmm_after;
    struct vmm_info vmm_before, vmm_after;
    if (!scheduler_get_stats(&before) || before.core.task_count != 2u ||
        !heap_get_stats(&heap_before) || !pmm_get_stats(&pmm_before) ||
        !vmm_get_info(&vmm_before) || pit_get_ticks() > UINT64_MAX - 10000u) {
        return false;
    }
    bool good = scheduler_validate() && heap_validate();
    uint64_t operations = 0u;
    uint64_t cpu_iterations = 0u;
    kprintf("Scheduler stress: deterministic workers, 16 lifecycle batches\n");
    good = run_workers(0u, 512u, &operations, &cpu_iterations) && good;
    struct scheduler_stats preempt_before, preempt_after;
    if (!scheduler_get_stats(&preempt_before)) {
        return false;
    }
    good = run_workers(1u, 0u, &operations, &cpu_iterations) && good;
    good = scheduler_get_stats(&preempt_after) &&
           preempt_after.timer_preemptions > preempt_before.timer_preemptions && good;
    good = run_workers(2u, 8u, &operations, &cpu_iterations) && good;
    for (unsigned int batch = 0u; batch < 16u; ++batch) {
        good = run_workers(0u, 16u, &operations, &cpu_iterations) && good;
    }
    /* Timer IRQs continue, but voluntary blocking and context switches do not. */
    struct test_worker held_worker = {.loops = 1u};
    uint64_t held_tid;
    preempt_disable();
    preempt_disable();
    if (!thread_create("deferred-worker", worker_main, &held_worker, &held_tid)) {
        PANIC("Cannot create deferred preemption test worker");
    }
    struct scheduler_stats held_before, held_after;
    if (!scheduler_get_stats(&held_before)) {
        PANIC("Cannot inspect deferred preemption");
    }
    const uint64_t stop = pit_get_ticks() + 5u;
    while (pit_get_ticks() < stop) {
        /* Bounded test of deferred preemption, never operational idle policy. */
    }
    good = !thread_yield() && !thread_sleep_ms(10u) &&
           held_worker.operations == 0u && !held_worker.done && good;
    preempt_enable();
    good = scheduler_get_stats(&held_after) &&
           held_after.core.switches == held_before.core.switches &&
           held_worker.operations == 0u && !held_worker.done && good;
    preempt_enable();
    const uint64_t cleanup_limit = pit_get_ticks() + 1000u;
    for (;;) {
        thread_reap();
        struct scheduler_stats cleanup;
        if (!scheduler_get_stats(&cleanup)) {
            PANIC("Cannot inspect deferred worker cleanup");
        }
        if (cleanup.core.task_count == 2u) {
            break;
        }
        if (pit_get_ticks() >= cleanup_limit || !thread_sleep_ms(10u)) {
            PANIC("Deferred worker did not exit");
        }
    }
    good = held_worker.done && !held_worker.failed &&
           held_worker.operations == 1u && good;
    ++operations;
    const uint64_t disabled = cpu_irq_save();
    uint64_t rejected_tid = UINT64_MAX;
    good = !thread_yield() && !thread_sleep_ms(1u) &&
           !thread_create("invalid-context", worker_main, NULL, &rejected_tid) &&
           rejected_tid == UINT64_MAX && good;
    cpu_irq_restore(disabled);
    good = scheduler_get_stats(&after) && heap_get_stats(&heap_after) &&
           pmm_get_stats(&pmm_after) && vmm_get_info(&vmm_after) && good;
    if (!good) {
        return false;
    }
    if (heap_after.mapped_bytes < heap_before.mapped_bytes ||
        vmm_after.table_pages < vmm_before.table_pages) {
        return false;
    }
    const uint64_t heap_pages =
        (heap_after.mapped_bytes - heap_before.mapped_bytes) / MEMORY_PAGE_SIZE;
    const uint64_t table_pages = vmm_after.table_pages - vmm_before.table_pages;
    good = after.core.task_count == 2u && after.core.zombie_count == 0u &&
           after.created - before.created == 77u &&
           after.exited - before.exited == 77u &&
           after.reaped - before.reaped == 77u &&
           after.core.switches - before.core.switches >= 4000u &&
           after.timer_preemptions > before.timer_preemptions &&
           heap_after.used_bytes == heap_before.used_bytes &&
           heap_after.live_allocations == heap_before.live_allocations &&
           pmm_after.used_frames >= pmm_before.used_frames &&
           pmm_after.used_frames - pmm_before.used_frames == heap_pages + table_pages &&
           scheduler_validate() && heap_validate();
    kprintf("Scheduler completed operations: %llu\nContext switch delta: %llu\n",
            (unsigned long long)operations,
            (unsigned long long)(after.core.switches - before.core.switches));
    kprintf("CPU-bound iterations: %llu\n",
            (unsigned long long)cpu_iterations);
    kprintf("Timer preemption delta: %llu\nReaped thread delta: %llu\n",
            (unsigned long long)(after.timer_preemptions - before.timer_preemptions),
            (unsigned long long)(after.reaped - before.reaped));
    return good;
}
