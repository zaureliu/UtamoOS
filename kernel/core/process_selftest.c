/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/heap.h>
#include <utamo/log.h>
#include <utamo/memory.h>
#include <utamo/paging.h>
#include <utamo/panic.h>
#include <utamo/pit.h>
#include <utamo/pmm.h>
#include <utamo/scheduler.h>
#include <utamo/user_probe.h>

static bool await_processes(const uint64_t *pids, size_t count,
                            struct process_result *results)
{
    const uint64_t now = pit_get_ticks();
    if (now > UINT64_MAX - 3000u) {
        PANIC("Process self-test clock cannot represent its deadline");
    }
    const uint64_t deadline = now + 3000u;
    for (;;) {
        thread_reap();
        struct process_stats stats;
        if (!process_get_stats(&stats)) {
            PANIC("Cannot inspect process cleanup");
        }
        if (stats.active == 0u) {
            break;
        }
        if (pit_get_ticks() >= deadline || !thread_sleep_ms(10u)) {
            PANIC("Process self-test exceeded bounded lifetime");
        }
    }
    bool good = true;
    for (size_t i = 0u; i < count; ++i) {
        good = process_get_result(pids[i], &results[i]) && good;
    }
    return good;
}

static bool pair_test(void)
{
    uint64_t pids[2] = {0};
    struct process_result results[2] = {0};
    size_t started = 0u;
    bool good = true;
    preempt_disable();
    for (size_t i = 0u; i < 2u; ++i) {
        if (!process_spawn_probe(UTAMO_PROBE_GOOD, &pids[i])) {
            good = false;
            break;
        }
        ++started;
    }
    struct vmm_mapping data[2], code, stack, guard;
    if (started == 2u) {
        good = pids[0] != pids[1] &&
            process_query(pids[0], UTAMO_USER_DATA, &data[0]) &&
            process_query(pids[1], UTAMO_USER_DATA, &data[1]) &&
            data[0].mapped && data[1].mapped &&
            data[0].physical != data[1].physical &&
            data[0].page_size == MEMORY_PAGE_SIZE &&
            data[1].page_size == MEMORY_PAGE_SIZE &&
            (data[0].flags & (VMM_USER | VMM_WRITABLE | VMM_NX)) ==
                (VMM_USER | VMM_WRITABLE | VMM_NX) &&
            process_query(pids[0], UTAMO_USER_CODE, &code) && code.mapped &&
            (code.flags & (VMM_USER | VMM_WRITABLE | VMM_NX)) == VMM_USER &&
            process_query(pids[0], UTAMO_USER_STACK_TOP - 1u, &stack) &&
            stack.mapped &&
            (stack.flags & (VMM_USER | VMM_WRITABLE | VMM_NX)) ==
                (VMM_USER | VMM_WRITABLE | VMM_NX) &&
            process_query(pids[0], UTAMO_USER_STACK_TOP -
                (UTAMO_USER_STACK_PAGES + 1u) * MEMORY_PAGE_SIZE, &guard) &&
            !guard.mapped && good;
    }
    preempt_enable();
    good = await_processes(pids, started, results) && good;
    for (size_t i = 0u; i < started; ++i) {
        good = !results[i].faulted && results[i].exit_code == 0 &&
               results[i].syscalls == UTAMO_PROBE_PID_LOOPS + 5u && good;
    }
    kprintf("User isolated pair: %s\n", (const char *)(good ? "PASS" : "FAIL"));
    return good && started == 2u;
}

static bool capacity_test(void)
{
    uint64_t pids[UTAMO_PROCESS_LIMIT] = {0};
    struct process_result results[UTAMO_PROCESS_LIMIT] = {0};
    uint64_t frames[UTAMO_PROCESS_LIMIT] = {0};
    size_t started = 0u;
    bool good = true;
    preempt_disable();
    for (size_t i = 0u; i < UTAMO_PROCESS_LIMIT; ++i) {
        if (!process_spawn_probe(UTAMO_PROBE_POINTERS, &pids[i])) {
            good = false;
            break;
        }
        ++started;
        struct vmm_mapping mapping;
        if (!process_query(pids[i], UTAMO_USER_DATA, &mapping) || !mapping.mapped) {
            good = false;
        } else {
            frames[i] = mapping.physical;
            for (size_t j = 0u; j < i; ++j) {
                good = frames[j] != frames[i] && pids[j] != pids[i] && good;
            }
        }
    }
    struct process_stats full = {0};
    struct pmm_stats before_reject = {0}, after_reject = {0};
    struct heap_stats heap_before_reject = {0}, heap_after_reject = {0};
    uint64_t rejected = UINT64_MAX;
    good = process_get_stats(&full) && full.active == UTAMO_PROCESS_LIMIT &&
        pmm_get_stats(&before_reject) && heap_get_stats(&heap_before_reject) && good;
    good = !process_spawn_probe(UTAMO_PROBE_POINTERS, &rejected) &&
           rejected == UINT64_MAX && good;
    good = pmm_get_stats(&after_reject) && heap_get_stats(&heap_after_reject) &&
        before_reject.used_frames == after_reject.used_frames &&
        heap_before_reject.used_bytes == heap_after_reject.used_bytes &&
        heap_before_reject.live_allocations == heap_after_reject.live_allocations && good;
    preempt_enable();
    good = await_processes(pids, started, results) && good;
    for (size_t i = 0u; i < started; ++i) {
        good = !results[i].faulted && results[i].exit_code == 0 &&
               results[i].syscalls == 10u && good;
    }
    kprintf("User process capacity: %s (16 active, extra creation rejected)\n",
            (const char *)(good ? "PASS" : "FAIL"));
    return good && started == UTAMO_PROCESS_LIMIT;
}

static bool one_probe(unsigned int probe)
{
    uint64_t pid;
    if (!process_spawn_probe(probe, &pid)) {
        return false;
    }
    struct process_result result = {0};
    if (!await_processes(&pid, 1u, &result)) {
        return false;
    }
    kprintf("User probe result: probe %u PID %llu exit %lld vector %llu error 0x%llx\n",
            probe, (unsigned long long)pid, (long long)result.exit_code,
            (unsigned long long)result.fault_vector,
            (unsigned long long)result.fault_error);
    if (probe == UTAMO_PROBE_POINTERS) {
        return !result.faulted && result.exit_code == 0 && result.syscalls == 10u;
    }
    uint64_t vector = 13u;
    uint64_t error = 0u;
    switch (probe) {
    case UTAMO_PROBE_KERNEL_READ: vector = 14u; error = 5u; break;
    case UTAMO_PROBE_KERNEL_WRITE: vector = 14u; error = 7u; break;
    case UTAMO_PROBE_NX: vector = 14u; error = 21u; break;
    case UTAMO_PROBE_UD2:
    case UTAMO_PROBE_SYSCALL: vector = 6u; break;
    case UTAMO_PROBE_DIV0: vector = 0u; break;
    case UTAMO_PROBE_INT240: error = UTAMO_SCHEDULE_VECTOR * 8u + 2u; break;
    case UTAMO_PROBE_FPU: vector = 7u; break;
    case UTAMO_PROBE_SYSENTER:
        /* Intel with zero SYSENTER_CS faults #GP; AMD long mode rejects #UD. */
        if (result.fault_vector == 6u) {
            vector = 6u;
        }
        break;
    default: break;
    }
    return result.faulted && result.fault_vector == vector &&
           result.fault_error == error &&
           result.exit_code == -(int64_t)(128u + vector);
}

bool process_selftest(void)
{
    struct process_stats before, after;
    struct scheduler_stats scheduler_before, scheduler_after;
    struct heap_stats heap_before, heap_after;
    struct pmm_stats pmm_before, pmm_after;
    struct vmm_info vmm_before, vmm_after;
    if (!process_available() || !process_get_stats(&before) || before.active != 0u ||
        !scheduler_get_stats(&scheduler_before) ||
        scheduler_before.core.task_count != 2u ||
        !heap_get_stats(&heap_before) || !pmm_get_stats(&pmm_before) ||
        !vmm_get_info(&vmm_before) || pit_get_ticks() > UINT64_MAX - 10000u) {
        return false;
    }
    const uint64_t initial_cr3 = cpu_read_cr3();
    bool good = heap_validate() && scheduler_validate();
    kprintf("User process stress: isolated pairs, 13 fault probes, 4 lifecycle batches\n");
    good = pair_test() && good;
    good = one_probe(UTAMO_PROBE_POINTERS) && good;
    good = capacity_test() && good;
    for (unsigned int probe = 1u; probe < UTAMO_PROBE_COUNT; ++probe) {
        if (probe != UTAMO_PROBE_POINTERS) {
            good = one_probe(probe) && good;
        }
    }
    for (unsigned int batch = 0u; batch < 4u; ++batch) {
        good = pair_test() && good;
    }
    if (!process_get_stats(&after) || !scheduler_get_stats(&scheduler_after) ||
        !heap_get_stats(&heap_after) || !pmm_get_stats(&pmm_after) ||
        !vmm_get_info(&vmm_after) ||
        heap_after.mapped_bytes < heap_before.mapped_bytes ||
        vmm_after.table_pages < vmm_before.table_pages ||
        pmm_after.used_frames < pmm_before.used_frames) {
        return false;
    }
    const uint64_t retained = (heap_after.mapped_bytes - heap_before.mapped_bytes) /
        MEMORY_PAGE_SIZE + vmm_after.table_pages - vmm_before.table_pages;
    good = after.active == 0u && after.created - before.created == 40u &&
        after.exited - before.exited == 40u &&
        after.reaped - before.reaped == 40u &&
        after.user_faults - before.user_faults == 13u &&
        after.syscalls - before.syscalls == 10u * (UTAMO_PROBE_PID_LOOPS + 5u) + 170u &&
        scheduler_after.core.task_count == 2u &&
        scheduler_after.core.zombie_count == 0u &&
        scheduler_after.user_timer_preemptions > scheduler_before.user_timer_preemptions &&
        scheduler_after.address_space_switches > scheduler_before.address_space_switches &&
        cpu_read_cr3() == initial_cr3 &&
        heap_after.used_bytes == heap_before.used_bytes &&
        heap_after.live_allocations == heap_before.live_allocations &&
        pmm_after.used_frames - pmm_before.used_frames == retained &&
        heap_validate() && scheduler_validate() && good;
    kprintf("User process created delta: %llu\nUser process reaped delta: %llu\n",
            (unsigned long long)(after.created - before.created),
            (unsigned long long)(after.reaped - before.reaped));
    kprintf("User fault delta: %llu\nUser syscall delta: %llu\n",
            (unsigned long long)(after.user_faults - before.user_faults),
            (unsigned long long)(after.syscalls - before.syscalls));
    kprintf("User timer preemption delta: %llu\nAddress-space switch delta: %llu\n",
            (unsigned long long)(scheduler_after.user_timer_preemptions -
                                 scheduler_before.user_timer_preemptions),
            (unsigned long long)(scheduler_after.address_space_switches -
                                 scheduler_before.address_space_switches));
    kprintf("User retained kernel pages: %llu\n",
            (unsigned long long)retained);
    return good;
}
