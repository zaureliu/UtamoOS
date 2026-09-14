/* SPDX-License-Identifier: MIT */
#include <utamo/shell.h>

#include <utamo/cpu.h>
#include <utamo/heap.h>
#include <utamo/interrupts.h>
#include <utamo/keyboard.h>
#include <utamo/log.h>
#include <utamo/memory.h>
#include <utamo/memory_selftest.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/process.h>
#include <utamo/serial.h>
#include <utamo/shell_line.h>
#include <utamo/string.h>
#include <utamo/version.h>
#include <utamo/vmm.h>

static const struct memory_map *system_memory;
static struct terminal *system_terminal;
static struct shell_line input_line;
static size_t line_limit;

static void show_prompt(void)
{
    shell_line_init(&input_line, line_limit);
    kprintf("utamo> ");
}

void shell_init(const struct memory_map *map, struct terminal *terminal)
{
    system_memory = map;
    system_terminal = terminal;
    line_limit = UTAMO_SHELL_LINE_CAPACITY - 1u;
    /*
     * Keep editing in one physical row. The bootstrap terminal deliberately
     * clears on bottom overflow and does not backspace into previous rows.
     */
    if (terminal != NULL && terminal->initialized) {
        line_limit = terminal->columns > 7u ? terminal->columns - 7u : 0u;
    }
    show_prompt();
}

static void show_physical_memory(void)
{
    struct pmm_stats stats;
    if (!pmm_get_stats(&stats)) {
        kprintf("Physical memory manager: unavailable\n");
        return;
    }
    kprintf("Physical Memory\n");
    kprintf("Managed frames: %llu\nUsed frames: %llu\nFree frames: %llu\n",
            (unsigned long long)stats.total_frames,
            (unsigned long long)stats.used_frames,
            (unsigned long long)stats.free_frames);
    kprintf("Managed: %llu KiB\nUsed: %llu KiB\nFree: %llu KiB\n",
            (unsigned long long)(stats.total_frames * (MEMORY_PAGE_SIZE / 1024u)),
            (unsigned long long)(stats.used_frames * (MEMORY_PAGE_SIZE / 1024u)),
            (unsigned long long)(stats.free_frames * (MEMORY_PAGE_SIZE / 1024u)));
    kprintf("Page size: %llu bytes\nBitmap physical: 0x%llx\n",
            (unsigned long long)MEMORY_PAGE_SIZE,
            (unsigned long long)stats.bitmap_phys);
    kprintf("Bitmap bytes: %llu\nBitmap storage: %llu bytes\n",
            (unsigned long long)stats.bitmap_bytes,
            (unsigned long long)stats.storage_bytes);
}

static void show_virtual_memory(void)
{
    struct vmm_info info;
    if (!vmm_get_info(&info)) {
        kprintf("Virtual memory manager: unavailable\n");
        return;
    }
    kprintf("Virtual Memory\nKernel base: 0x%llx\nCR3 root: 0x%llx\n",
            (unsigned long long)info.kernel_base,
            (unsigned long long)info.root_phys);
    kprintf("HHDM offset: 0x%llx\nPhysical address bits: %u\n",
            (unsigned long long)info.hhdm_offset, info.physical_bits);
    kprintf("NX supported: %s\nNX enabled: %s\nSection protections: %s\n",
            (const char *)(info.nx_supported ? "Yes" : "No"),
            (const char *)(info.nx_enabled ? "Yes" : "No"),
            (const char *)(info.sections_protected ? "Applied" : "Not applied"));
    kprintf("PMM-owned page tables: %llu\n",
            (unsigned long long)info.table_pages);
}

static void show_mapping(char *arguments)
{
    char *cursor = arguments;
    const char *token = shell_next_token(&cursor);
    uint64_t address;
    if (token == NULL || shell_next_token(&cursor) != NULL ||
        !shell_parse_u64_hex(token, &address)) {
        kprintf("Usage: mapinfo <hexadecimal virtual address>\n");
        return;
    }
    struct vmm_mapping mapping;
    kprintf("Virtual: 0x%llx\n", (unsigned long long)address);
    if (!vmm_query_page(address, &mapping)) {
        kprintf("VMM query unavailable or address invalid.\n");
        return;
    }
    kprintf("Mapped: %s\n", (const char *)(mapping.mapped ? "Yes" : "No"));
    if (!mapping.mapped) {
        return;
    }
    kprintf("Physical: 0x%llx\nPage size: %llu bytes\nEffective flags: 0x%llx\n",
            (unsigned long long)mapping.physical,
            (unsigned long long)mapping.page_size,
            (unsigned long long)mapping.flags);
    kprintf("Present: %s\nWritable: %s\nUser: %s\nNX: %s\n",
            (const char *)((mapping.flags & VMM_PRESENT) != 0u ? "Yes" : "No"),
            (const char *)((mapping.flags & VMM_WRITABLE) != 0u ? "Yes" : "No"),
            (const char *)((mapping.flags & VMM_USER) != 0u ? "Yes" : "No"),
            (const char *)((mapping.flags & VMM_NX) != 0u ? "Yes" : "No"));
}

static void show_heap(void)
{
    struct heap_stats stats;
    if (!heap_get_stats(&stats)) {
        kprintf("Kernel heap: unavailable\n");
        return;
    }
    kprintf("Kernel Heap\nHeap base: 0x%llx\n",
            (unsigned long long)UTAMO_HEAP_BASE);
    kprintf("Mapped bytes: %llu\nUsed bytes: %llu\nFree bytes: %llu\nOverhead bytes: %llu\n",
            (unsigned long long)stats.mapped_bytes,
            (unsigned long long)stats.used_bytes,
            (unsigned long long)stats.free_bytes,
            (unsigned long long)stats.overhead_bytes);
    kprintf("Live allocations: %llu\nAllocations: %llu\nFrees: %llu\nPeak usage: %llu\n",
            (unsigned long long)stats.live_allocations,
            (unsigned long long)stats.allocations,
            (unsigned long long)stats.frees,
            (unsigned long long)stats.peak_usage);
    kprintf("Failed allocations: %llu\nInvalid frees: %llu\nLargest free block: %llu\n",
            (unsigned long long)stats.failed_allocations,
            (unsigned long long)stats.invalid_frees,
            (unsigned long long)stats.largest_free_bytes);
    kprintf("Heap integrity: %s\n",
            (const char *)(heap_validate() ? "OK" : "FAILED"));
}

static void show_threads(void)
{
    struct thread_snapshot threads[UTAMO_SCHED_MAX_TASKS];
    const size_t count = scheduler_list(threads, UTAMO_SCHED_MAX_TASKS);
    if (count == 0u) {
        kprintf("Scheduler: unavailable\n");
        return;
    }
    kprintf("TID STATE NAME\n");
    for (size_t i = 0u; i < count; ++i) {
        kprintf("%llu %s %s\n", (unsigned long long)threads[i].id,
                sched_state_name(threads[i].state),
                (const char *)threads[i].name);
    }
}

static void show_scheduler(void)
{
    struct scheduler_stats stats;
    if (!scheduler_get_stats(&stats)) {
        kprintf("Scheduler: unavailable\n");
        return;
    }
    kprintf("Scheduler\nThreads: %llu\nCurrent TID: %llu\nQuantum ticks: %u\n",
            (unsigned long long)stats.core.task_count,
            (unsigned long long)stats.core.current_id,
            (unsigned int)stats.core.quantum_ticks);
    kprintf("Ready threads: %llu\nSleeping threads: %llu\nBlocked threads: %llu\nZombie threads: %llu\n",
            (unsigned long long)stats.core.ready_count,
            (unsigned long long)stats.core.sleeping_count,
            (unsigned long long)stats.core.blocked_count,
            (unsigned long long)stats.core.zombie_count);
    kprintf("Context switches: %llu\nTimer preemptions: %llu\n",
            (unsigned long long)stats.core.switches,
            (unsigned long long)stats.timer_preemptions);
    kprintf("Threads created: %llu\nThreads exited: %llu\nThreads reaped: %llu\n",
            (unsigned long long)stats.created,
            (unsigned long long)stats.exited,
            (unsigned long long)stats.reaped);
    kprintf("Scheduler integrity: %s\n",
            (const char *)(scheduler_validate() ? "OK" : "FAILED"));
}

static void run_sleep(char *arguments)
{
    char *cursor = arguments;
    const char *token = shell_next_token(&cursor);
    uint64_t milliseconds;
    if (token == NULL || shell_next_token(&cursor) != NULL ||
        !shell_parse_u64_dec(token, &milliseconds)) {
        kprintf("Usage: sleep <decimal-ms>\n");
        return;
    }
    if (!thread_sleep_ms(milliseconds)) {
        kprintf("Sleep failed.\n");
        return;
    }
    kprintf("Sleep completed.\n");
}

static void show_processes(void)
{
    struct process_stats stats;
    struct scheduler_stats sched;
    if (!process_get_stats(&stats) || !scheduler_get_stats(&sched)) {
        kprintf("Process diagnostics unavailable.\n");
        return;
    }
    kprintf("Processes\nAvailable: %s\nActive processes: %llu\n",
            (const char *)(stats.status == UTAMO_USER_READY ? "yes" : "no"),
            (unsigned long long)stats.active);
    kprintf("Processes created: %llu\nProcesses exited: %llu\nProcesses reaped: %llu\n",
            (unsigned long long)stats.created, (unsigned long long)stats.exited,
            (unsigned long long)stats.reaped);
    kprintf("User faults: %llu\nSyscalls: %llu\nUser timer preemptions: %llu\n",
            (unsigned long long)stats.user_faults, (unsigned long long)stats.syscalls,
            (unsigned long long)sched.user_timer_preemptions);
    kprintf("Address-space switches: %llu\n",
            (unsigned long long)sched.address_space_switches);
}

static void run_user_test(void)
{
    struct process_stats stats;
    if (!process_get_stats(&stats)) {
        kprintf("Process diagnostics unavailable.\n");
    } else if (stats.status == UTAMO_USER_NO_NX) {
        kprintf("User processes unavailable: NX is required\n");
    } else if (stats.status != UTAMO_USER_READY) {
        kprintf("User processes unavailable: unsupported CPU/paging configuration\n");
    } else {
        kprintf("User process self-test: %s\n",
                (const char *)(process_selftest() ? "PASS" : "FAIL"));
    }
}

static void show_memory(void)
{
    kprintf("Memory map entries: %llu\nUsable memory: %llu MiB (%llu bytes)\n",
            (unsigned long long)system_memory->count,
            (unsigned long long)(system_memory->usable_bytes / (1024u * 1024u)),
            (unsigned long long)system_memory->usable_bytes);
    kprintf("Bootloader reclaimable: %llu KiB\n",
            (unsigned long long)(system_memory->bootloader_reclaimable_bytes /
                                  1024u));
    show_physical_memory();
    show_virtual_memory();
}

static void show_sysinfo(void)
{
    kprintf("UTAMO OS %s\nArchitecture: x86_64\nBootloader: Limine\n",
            (const char *)UTAMO_VERSION);
    show_memory();
    kprintf("Timer: PIT %u Hz (nominal)\nTicks: %llu\n",
            (unsigned int)UTAMO_PIT_HZ, (unsigned long long)pit_get_ticks());
    if (system_terminal != NULL && system_terminal->initialized) {
        const struct framebuffer *fb = system_terminal->framebuffer;
        kprintf("Framebuffer: %llux%llu, %u bpp\n",
                (unsigned long long)fb->width,
                (unsigned long long)fb->height,
                (unsigned int)(fb->bytes_per_pixel * 8u));
    }
}

static void run_fault(char *arguments)
{
    char *cursor = arguments;
    const char *kind = shell_next_token(&cursor);
    if (kind == NULL || shell_next_token(&cursor) != NULL) {
        kprintf("Usage: fault ud2|div0|pf|vmm|stack (fatal; restart QEMU afterwards)\n");
        return;
    }
    if (strcmp(kind, "ud2") == 0) {
        exception_fault_ud2();
    } else if (strcmp(kind, "div0") == 0) {
        exception_fault_div0();
    } else if (strcmp(kind, "pf") == 0) {
        exception_fault_page();
    } else if (strcmp(kind, "stack") == 0) {
        scheduler_fault_guard();
    } else if (strcmp(kind, "vmm") == 0) {
        memory_fault_unmapped();
    } else {
        kprintf("Usage: fault ud2|div0|pf|vmm|stack (fatal; restart QEMU afterwards)\n");
    }
}

static void execute_line(void)
{
    struct shell_command command;
    if (!shell_parse_line(input_line.bytes, &command)) {
        return;
    }
    const char *name = command.name;
    if (strcmp(name, "echo") == 0) {
        kprintf("%s\n", (const char *)command.arguments);
        return;
    }
    if (strcmp(name, "fault") == 0) {
        run_fault(command.arguments);
        return;
    }
    if (strcmp(name, "mapinfo") == 0) {
        show_mapping(command.arguments);
        return;
    }
    if (strcmp(name, "sleep") == 0) {
        run_sleep(command.arguments);
        return;
    }
    if (*command.arguments != '\0') {
        kprintf("Unexpected arguments. Type help.\n");
        return;
    }
    if (strcmp(name, "help") == 0) {
        kprintf("help     List implemented commands\n");
        kprintf("clear    Clear framebuffer and serial terminal\n");
        kprintf("version  Kernel version\n");
        kprintf("sysinfo  Known boot and hardware information\n");
        kprintf("mem      Boot memory map, PMM and VMM statistics\n");
        kprintf("pmm      Physical frame allocator statistics\n");
        kprintf("vmm      Virtual memory configuration\n");
        kprintf("mapinfo  Query a hexadecimal virtual address\n");
        kprintf("pmmtest  Bounded physical frame self-test\n");
        kprintf("vmmtest  Bounded virtual mapping self-test\n");
        kprintf("heap     Kernel heap accounting and integrity\n");
        kprintf("heaptest Bounded deterministic heap stress\n");
        kprintf("ps       List scheduled thread snapshots\n");
        kprintf("threads  Alias for ps\n");
        kprintf("schedulerstats Scheduler counters and integrity\n");
        kprintf("schedtest Bounded scheduler self-test\n");
        kprintf("processes Native user process accounting\n");
        kprintf("usertest Bounded Ring 3 isolation and fault tests\n");
        kprintf("sleep    Block this thread for decimal milliseconds\n");
        kprintf("uptime   PIT uptime and ticks\n");
        kprintf("echo     Repeat following text\n");
        kprintf("halt     Disable interrupts and stop CPU\n");
        kprintf("fault    ud2, div0 or pf; vmm or stack: guard/unmapped page (fatal)\n");
    } else if (strcmp(name, "clear") == 0) {
        preempt_disable();
        terminal_clear(system_terminal);
        /* ANSI is for the external serial terminal, not the bitmap renderer. */
        (void)serial_write_string("\x1b[2J\x1b[H");
        preempt_enable();
    } else if (strcmp(name, "version") == 0) {
        kprintf("UTAMO OS %s\n", (const char *)UTAMO_VERSION);
    } else if (strcmp(name, "sysinfo") == 0) {
        show_sysinfo();
    } else if (strcmp(name, "mem") == 0) {
        show_memory();
    } else if (strcmp(name, "pmm") == 0) {
        show_physical_memory();
    } else if (strcmp(name, "vmm") == 0) {
        show_virtual_memory();
    } else if (strcmp(name, "pmmtest") == 0) {
        kprintf("PMM self-test: %s\n",
                (const char *)(memory_pmm_selftest() ? "PASS" : "FAIL"));
    } else if (strcmp(name, "vmmtest") == 0) {
        kprintf("VMM self-test: %s\n",
                (const char *)(memory_vmm_selftest() ? "PASS" : "FAIL"));
    } else if (strcmp(name, "heap") == 0) {
        show_heap();
    } else if (strcmp(name, "heaptest") == 0) {
        kprintf("Heap self-test: %s\n",
                (const char *)(heap_selftest() ? "PASS" : "FAIL"));
    } else if (strcmp(name, "ps") == 0 || strcmp(name, "threads") == 0) {
        show_threads();
    } else if (strcmp(name, "schedulerstats") == 0) {
        show_scheduler();
    } else if (strcmp(name, "schedtest") == 0) {
        kprintf("Scheduler self-test: %s\n",
                (const char *)(scheduler_selftest() ? "PASS" : "FAIL"));
    } else if (strcmp(name, "processes") == 0) {
        show_processes();
    } else if (strcmp(name, "usertest") == 0) {
        run_user_test();
    } else if (strcmp(name, "uptime") == 0) {
        const uint64_t ticks = pit_get_ticks();
        const uint64_t seconds = pit_ticks_to_seconds(ticks);
        kprintf("Uptime: %lluh %llum %llus (%llu ticks; PIT nominal %u Hz)\n",
                (unsigned long long)(seconds / 3600u),
                (unsigned long long)((seconds / 60u) % 60u),
                (unsigned long long)(seconds % 60u),
                (unsigned long long)ticks, (unsigned int)UTAMO_PIT_HZ);
    } else if (strcmp(name, "halt") == 0) {
        cpu_disable_interrupts();
        kprintf("System halted.\n");
        cpu_halt();
    } else {
        kprintf("Unknown command: %s. Type help.\n", name);
    }
}

void shell_process_input(void)
{
    char character;
    /* The outer loop can service other future work between finite batches. */
    for (size_t i = 0u; i < UTAMO_SHELL_LINE_CAPACITY &&
         keyboard_read_char(&character); ++i) {
        const enum shell_edit action = shell_line_feed(&input_line, character);
        if (action == UTAMO_SHELL_APPENDED) {
            kprintf("%c", (int)input_line.bytes[input_line.length - 1u]);
        } else if (action == UTAMO_SHELL_ERASED) {
            preempt_disable();
            terminal_putc(system_terminal, '\b');
            (void)serial_write_string("\b \b");
            preempt_enable();
        } else if (action == UTAMO_SHELL_SUBMITTED) {
            kprintf("\n");
            execute_line();
            show_prompt();
        }
    }
}
