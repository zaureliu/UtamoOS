/* SPDX-License-Identifier: MIT */
/* Host-only shell integration: hardware effects are explicit test doubles. */
#include <utamo/shell.h>
#include <utamo/filesystem.h>
#include <utamo/storage.h>
#include <utamo/pci.h>

#include <setjmp.h>
#include <stdio.h>
#include <utamo/cpu.h>
#include <utamo/heap.h>
#include <utamo/interrupts.h>
#include <utamo/keyboard.h>
#include <utamo/log.h>
#include <utamo/memory_selftest.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/process.h>
#include <utamo/scheduler.h>
#include <utamo/serial.h>
#include <utamo/string.h>
#include <utamo/version.h>
#include <utamo/vmm.h>

static unsigned int checks;
static unsigned int failures;
static char output[16384];
static size_t output_length;
static char serial_output[128];
static size_t serial_length;
static const char *pending_input;
static unsigned int clears;
static unsigned int backspaces;
static unsigned int interrupt_disables;
static uint64_t timer_ticks = 366123u;
static jmp_buf stop_target;
static bool heap_ready = true;
static bool heap_valid = true;
static bool heap_test_passes = true;
static unsigned int heap_test_calls;
static bool pmm_ready = true;
static bool vmm_ready = true;
static bool mock_mapped = true;
static bool pmm_test_passes = true;
static bool vmm_test_passes = true;
static unsigned int pmm_test_calls;
static unsigned int vmm_test_calls;
static unsigned int query_calls;
static uint64_t queried_address;
static uint64_t mock_flags = VMM_PRESENT | VMM_NX;
static uint64_t mock_page_size = 4096u;
static bool process_ready = true;
static bool process_test_passes = true;
static enum arch_user_status process_status = UTAMO_USER_READY;
static unsigned int process_stats_calls;
static unsigned int process_test_calls;
static bool scheduler_ready = true;
static bool scheduler_valid = true;
static bool scheduler_test_passes = true;
static bool sleep_passes = true;
static unsigned int scheduler_list_calls;
static unsigned int scheduler_stats_calls;
static unsigned int scheduler_test_calls;
static unsigned int sleep_calls;
static uint64_t slept_milliseconds;
static size_t listed_capacity;
static unsigned int preempt_depth;
static unsigned int preempt_disables;
static unsigned int preempt_enables;
static unsigned int preempt_unbalanced;
static unsigned int unprotected_direct_io;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

static void capture(char character, void *context)
{
    (void)context;
    if (output_length + 1u < sizeof(output)) {
        output[output_length] = character;
        ++output_length;
        output[output_length] = '\0';
    }
}

void kprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    kvformat(capture, NULL, format, args);
    va_end(args);
}

bool serial_write_string(const char *text)
{
    if (preempt_depth == 0u) {
        ++unprotected_direct_io;
    }
    while (*text != '\0' && serial_length + 1u < sizeof(serial_output)) {
        serial_output[serial_length] = *text;
        ++serial_length;
        ++text;
    }
    serial_output[serial_length] = '\0';
    return *text == '\0';
}

void terminal_clear(struct terminal *terminal)
{
    CHECK(terminal != NULL && terminal->initialized);
    if (preempt_depth == 0u) {
        ++unprotected_direct_io;
    }
    ++clears;
}

void terminal_putc(struct terminal *terminal, char character)
{
    CHECK(terminal != NULL && terminal->initialized && character == '\b');
    if (preempt_depth == 0u) {
        ++unprotected_direct_io;
    }
    ++backspaces;
}

bool keyboard_read_char(char *character)
{
    if (pending_input == NULL || *pending_input == '\0') {
        return false;
    }
    *character = *pending_input;
    ++pending_input;
    return true;
}

uint64_t pit_get_ticks(void)
{
    return timer_ticks;
}

uint64_t pit_ticks_to_seconds(uint64_t ticks)
{
    return ticks / UTAMO_PIT_HZ;
}

void cpu_disable_interrupts(void)
{
    ++interrupt_disables;
}

_Noreturn void cpu_halt(void)
{
    longjmp(stop_target, 1);
}

_Noreturn void exception_fault_ud2(void)
{
    longjmp(stop_target, 2);
}

_Noreturn void exception_fault_div0(void)
{
    longjmp(stop_target, 3);
}

_Noreturn void exception_fault_page(void)
{
    longjmp(stop_target, 4);
}

_Noreturn void memory_fault_unmapped(void)
{
    longjmp(stop_target, 5);
}

_Noreturn void scheduler_fault_guard(void)
{
    longjmp(stop_target, 6);
}

bool heap_get_stats(struct heap_stats *stats)
{
    if (!heap_ready) {
        return false;
    }
    *stats = (struct heap_stats){
        .mapped_bytes = 65536u, .used_bytes = 256u, .free_bytes = 65000u,
        .overhead_bytes = 280u, .live_allocations = 2u, .allocations = 7u,
        .frees = 5u, .peak_usage = 8192u, .failed_allocations = 3u,
        .invalid_frees = 1u, .largest_free_bytes = 64000u
    };
    return true;
}

bool heap_validate(void)
{
    return heap_valid;
}

bool heap_selftest(void)
{
    ++heap_test_calls;
    return heap_test_passes;
}

void preempt_disable(void)
{
    ++preempt_depth;
    ++preempt_disables;
}

void preempt_enable(void)
{
    ++preempt_enables;
    if (preempt_depth == 0u) {
        ++preempt_unbalanced;
    } else {
        --preempt_depth;
    }
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

size_t scheduler_list(struct thread_snapshot *out, size_t capacity)
{
    ++scheduler_list_calls;
    listed_capacity = capacity;
    static const struct thread_snapshot snapshots[] = {
        {.id = 0u, .state = UTAMO_SCHED_READY, .name = "idle", .idle = true},
        {.id = 1u, .state = UTAMO_SCHED_RUNNING, .name = "shell", .current = true},
        {.id = 42u, .state = UTAMO_SCHED_READY, .name = "worker%s"},
        {.id = 43u, .state = UTAMO_SCHED_BLOCKED, .name = "waiter"},
        {.id = 44u, .state = UTAMO_SCHED_SLEEPING, .name = "sleeper"},
        {.id = 45u, .state = UTAMO_SCHED_ZOMBIE, .name = "retired"}
    };
    if (!scheduler_ready || out == NULL) {
        return 0u;
    }
    const size_t available = sizeof(snapshots) / sizeof(snapshots[0]);
    const size_t count = available < capacity ? available : capacity;
    memcpy(out, snapshots, count * sizeof(*out));
    return count;
}

bool scheduler_get_stats(struct scheduler_stats *out)
{
    ++scheduler_stats_calls;
    if (!scheduler_ready || out == NULL) {
        return false;
    }
    *out = (struct scheduler_stats){
        .core = {
            .task_count = 6u, .current_id = 1u, .quantum_ticks = 5u,
            .ready_count = 1u, .sleeping_count = 1u,
            .blocked_count = 1u, .zombie_count = 1u,
            .switches = UINT64_C(4294967297)
        },
        .timer_preemptions = 123u, .created = 6u, .exited = 3u, .reaped = 2u,
        .user_timer_preemptions = UINT64_C(4294967301),
        .address_space_switches = UINT64_C(8589934600)
    };
    return true;
}

bool process_get_stats(struct process_stats *out)
{
    ++process_stats_calls;
    if (!process_ready || out == NULL) {
        return false;
    }
    *out = (struct process_stats){
        .created = UINT64_C(4294967299), .exited = 2u, .reaped = 1u,
        .user_faults = 17u, .syscalls = UINT64_MAX, .active = 3u,
        .status = process_status
    };
    return true;
}

bool process_selftest(void)
{
    ++process_test_calls;
    return process_test_passes;
}

bool scheduler_validate(void)
{
    return scheduler_valid;
}

bool scheduler_selftest(void)
{
    ++scheduler_test_calls;
    return scheduler_test_passes;
}

bool thread_sleep_ms(uint64_t milliseconds)
{
    ++sleep_calls;
    slept_milliseconds = milliseconds;
    if (preempt_depth != 0u) {
        ++preempt_unbalanced;
    }
    return sleep_passes;
}

bool pmm_get_stats(struct pmm_stats *stats)
{
    if (!pmm_ready) {
        return false;
    }
    *stats = (struct pmm_stats){
        .total_frames = 1000u, .used_frames = 32u, .free_frames = 968u,
        .bitmap_phys = UINT64_C(0x100000), .bitmap_bytes = 250u,
        .storage_bytes = 4096u, .span_frames = 2000u
    };
    return true;
}

bool vmm_get_info(struct vmm_info *info)
{
    if (!vmm_ready) {
        return false;
    }
    *info = (struct vmm_info){
        .root_phys = UINT64_C(0x1000),
        .hhdm_offset = UINT64_C(0xffff800000000000),
        .kernel_base = UINT64_C(0xffffffff80000000),
        .table_pages = 3u, .physical_bits = 40u,
        .nx_supported = true, .nx_enabled = true, .sections_protected = true
    };
    return true;
}

bool vmm_query_page(uint64_t address, struct vmm_mapping *mapping)
{
    ++query_calls;
    queried_address = address;
    if (!vmm_ready || (address > UINT64_C(0x00007fffffffffff) &&
                       address < UINT64_C(0xffff800000000000))) {
        return false;
    }
    *mapping = (struct vmm_mapping){
        .mapped = mock_mapped,
        .physical = UINT64_C(0x200000) + (address & UINT64_C(0xfff)),
        .flags = mock_flags, .page_size = mock_page_size
    };
    return true;
}

bool memory_pmm_selftest(void)
{
    ++pmm_test_calls;
    return pmm_test_passes;
}

bool memory_vmm_selftest(void)
{
    ++vmm_test_calls;
    return vmm_test_passes;
}

static bool contains(const char *text)
{
    const size_t length = strlen(text);
    for (size_t i = 0u; i <= output_length; ++i) {
        if (strncmp(output + i, text, length) == 0) {
            return true;
        }
    }
    return false;
}

static void issue(const char *text)
{
    output_length = 0u;
    output[0] = '\0';
    serial_length = 0u;
    serial_output[0] = '\0';
    pending_input = text;
    shell_process_input();
}

static void test_commands(void)
{
    static struct memory_map map = {
        .count = 18u,
        .usable_bytes = UINT64_C(254) * 1024u * 1024u,
        .bootloader_reclaimable_bytes = UINT64_C(512) * 1024u
    };
    static struct framebuffer framebuffer = {
        .width = 1024u, .height = 768u, .bytes_per_pixel = 4u,
        .initialized = true
    };
    static struct terminal terminal = {
        .framebuffer = &framebuffer, .columns = 128u, .rows = 48u,
        .initialized = true
    };
    shell_init(&map, &terminal);
    CHECK(strcmp(output, "utamo> ") == 0);
    issue("help\n");
    CHECK(contains("help     List implemented commands"));
    CHECK(contains("clear    Clear framebuffer"));
    CHECK(contains("version  Kernel version"));
    CHECK(contains("sysinfo  Known boot"));
    CHECK(contains("mem      Boot memory"));
    CHECK(contains("pmm      Physical frame"));
    CHECK(contains("vmm      Virtual memory"));
    CHECK(contains("mapinfo  Query"));
    CHECK(contains("pmmtest  Bounded physical"));
    CHECK(contains("vmmtest  Bounded virtual"));
    CHECK(contains("uptime   PIT uptime"));
    CHECK(contains("echo     Repeat"));
    CHECK(contains("halt     Disable"));
    CHECK(contains("fault    ud2, div0 or pf"));
    CHECK(contains("utamo> "));
    issue("version\n");
    CHECK(contains("UTAMO OS " UTAMO_VERSION "\n"));
    issue("sysinfo\n");
    CHECK(contains("UTAMO OS " UTAMO_VERSION "\nArchitecture: x86_64"));
    CHECK(contains("Bootloader: Limine"));
    CHECK(contains("Usable memory: 254 MiB (266338304 bytes)"));
    CHECK(contains("Timer: PIT 100 Hz"));
    CHECK(contains("Ticks: 366123"));
    CHECK(contains("Framebuffer: 1024x768, 32 bpp"));
    issue("mem\n");
    CHECK(contains("Memory map entries: 18"));
    CHECK(contains("Bootloader reclaimable: 512 KiB"));
    issue("uptime\n");
    CHECK(contains("Uptime: 1h 1m 1s (366123 ticks"));
    timer_ticks = 0u;
    issue("uptime\n");
    CHECK(contains("Uptime: 0h 0m 0s (0 ticks"));
    issue("echo hello  %s %x\n");
    CHECK(contains("\nhello  %s %x\nutamo> "));
    issue("echo\n");
    CHECK(strcmp(output, "echo\n\nutamo> ") == 0);
    issue("clear\n");
    CHECK(clears == 1u);
    CHECK(strcmp(serial_output, "\x1b[2J\x1b[H") == 0);
    CHECK(contains("utamo> "));
    issue("versiox\bn\n");
    CHECK(backspaces == 1u);
    CHECK(strcmp(serial_output, "\b \b") == 0);
    CHECK(contains("UTAMO OS " UTAMO_VERSION));
    issue("\b\bversion\n");
    CHECK(backspaces == 1u); /* Prompt protected from backspace. */
    CHECK(contains("UTAMO OS " UTAMO_VERSION));
    char full_line[145];
    for (size_t i = 0u; i < sizeof(full_line); ++i) {
        full_line[i] = 'a';
    }
    full_line[0] = 'e'; full_line[1] = 'c'; full_line[2] = 'h';
    full_line[3] = 'o'; full_line[4] = ' ';
    for (size_t i = 121u; i < sizeof(full_line) - 2u; ++i) {
        full_line[i] = 'X';
    }
    full_line[sizeof(full_line) - 2u] = '\n';
    full_line[sizeof(full_line) - 1u] = '\0';
    issue(full_line);
    shell_process_input(); /* Finite batch boundary; process the queued Enter. */
    CHECK(!contains("X"));
    CHECK(contains("utamo> "));
    CHECK(output_length == 121u + 1u + 116u + 1u + 7u);
    issue("halt surprise\n");
    CHECK(interrupt_disables == 0u);
    CHECK(contains("Unexpected arguments."));
    issue("fault ud2 surprise\n");
    CHECK(contains("Usage: fault"));
    issue("fault\n");
    CHECK(contains("Usage: fault"));
    issue("fault unknown\n");
    CHECK(contains("Usage: fault"));
    issue("missing\n");
    CHECK(contains("Unknown command: missing."));
    issue(" \t \n");
    CHECK(contains("utamo> "));
}

static void test_memory_commands(void)
{
    issue("mem\n");
    CHECK(contains("Memory map entries: 18"));
    CHECK(contains("Usable memory: 254 MiB"));
    CHECK(contains("Managed frames: 1000\nUsed frames: 32\nFree frames: 968"));
    CHECK(contains("Managed: 4000 KiB\nUsed: 128 KiB\nFree: 3872 KiB"));
    CHECK(contains("Page size: 4096 bytes"));
    CHECK(contains("Bitmap physical: 0x100000"));
    CHECK(contains("Bitmap bytes: 250\nBitmap storage: 4096 bytes"));
    CHECK(contains("Kernel base: 0xffffffff80000000"));
    CHECK(contains("CR3 root: 0x1000"));
    CHECK(contains("HHDM offset: 0xffff800000000000"));
    CHECK(contains("Physical address bits: 40"));
    CHECK(contains("NX supported: Yes\nNX enabled: Yes"));
    CHECK(contains("Section protections: Applied"));
    CHECK(contains("PMM-owned page tables: 3"));
    issue("pmm\n");
    CHECK(contains("Physical Memory\nManaged frames: 1000"));
    CHECK(!contains("Virtual Memory"));
    issue("vmm\n");
    CHECK(contains("Virtual Memory\nKernel base:"));
    CHECK(!contains("Physical Memory"));
    pmm_ready = false;
    issue("pmm\n");
    CHECK(contains("Physical memory manager: unavailable"));
    pmm_ready = true;
    vmm_ready = false;
    issue("vmm\n");
    CHECK(contains("Virtual memory manager: unavailable"));
    issue("mapinfo 0x0\n");
    CHECK(contains("VMM query unavailable or address invalid."));
    CHECK(!contains("Mapped: No"));
    vmm_ready = true;

    const unsigned int calls_before_invalid = query_calls;
    static const char *const invalid[] = {
        "mapinfo\n", "mapinfo 0x\n", "mapinfo -1\n", "mapinfo +1\n",
        "mapinfo 12q\n", "mapinfo 0x10000000000000000\n",
        "mapinfo 0x1234 extra\n", "mapinfo 1234 5678\n"
    };
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        issue(invalid[i]);
        CHECK(contains("Usage: mapinfo"));
    }
    CHECK(query_calls == calls_before_invalid);
    issue("mapinfo 0000800000000000\n");
    CHECK(queried_address == UINT64_C(0x0000800000000000));
    CHECK(contains("VMM query unavailable or address invalid."));
    issue("mapinfo 0XFFFFFFFF80000007\n");
    CHECK(queried_address == UINT64_C(0xffffffff80000007));
    CHECK(contains("Virtual: 0xffffffff80000007"));
    CHECK(contains("Mapped: Yes\nPhysical: 0x200007"));
    CHECK(contains("Page size: 4096 bytes"));
    CHECK(contains("Effective flags: 0x8000000000000001"));
    CHECK(contains("Present: Yes\nWritable: No\nUser: No\nNX: Yes"));
    mock_flags = VMM_PRESENT | VMM_WRITABLE | VMM_USER;
    mock_page_size = UINT64_C(2) * 1024u * 1024u;
    issue("mapinfo ffff800000000000\n");
    CHECK(contains("Page size: 2097152 bytes"));
    CHECK(contains("Present: Yes\nWritable: Yes\nUser: Yes\nNX: No"));
    mock_mapped = false;
    issue("mapinfo 0x1234\n");
    CHECK(contains("Mapped: No"));
    CHECK(!contains("Physical:"));
    CHECK(!contains("Effective flags:"));
    mock_mapped = true;

    issue("pmmtest extra\n");
    CHECK(pmm_test_calls == 0u);
    CHECK(contains("Unexpected arguments."));
    issue("vmmtest extra\n");
    CHECK(vmm_test_calls == 0u);
    CHECK(contains("Unexpected arguments."));
    issue("pmmtest\n");
    CHECK(pmm_test_calls == 1u && contains("PMM self-test: PASS"));
    pmm_test_passes = false;
    issue("pmmtest\n");
    CHECK(pmm_test_calls == 2u && contains("PMM self-test: FAIL"));
    issue("vmmtest\n");
    CHECK(vmm_test_calls == 1u && contains("VMM self-test: PASS"));
    vmm_test_passes = false;
    issue("vmmtest\n");
    CHECK(vmm_test_calls == 2u && contains("VMM self-test: FAIL"));
    CHECK(contains("utamo> "));
    issue("fault vmm extra\n");
    CHECK(contains("Usage: fault"));
}

static void test_heap_commands(void)
{
    issue("help\n");
    CHECK(contains("heap     Kernel heap accounting"));
    CHECK(contains("heaptest Bounded deterministic"));
    issue("heap extra\n");
    CHECK(contains("Unexpected arguments."));
    issue("heap\n");
    CHECK(contains("Kernel Heap\nHeap base: 0xffffc00001000000"));
    CHECK(contains("Mapped bytes: 65536\nUsed bytes: 256\nFree bytes: 65000"));
    CHECK(contains("Overhead bytes: 280\nLive allocations: 2"));
    CHECK(contains("Allocations: 7\nFrees: 5\nPeak usage: 8192"));
    CHECK(contains("Failed allocations: 3\nInvalid frees: 1"));
    CHECK(contains("Largest free block: 64000"));
    CHECK(contains("Heap integrity: OK"));
    heap_valid = false;
    issue("heap\n");
    CHECK(contains("Heap integrity: FAILED") && !contains("Heap integrity: OK"));
    heap_valid = true;
    heap_ready = false;
    issue("heap\n");
    CHECK(contains("Kernel heap: unavailable") && !contains("Mapped bytes:"));
    heap_ready = true;
    issue("heaptest extra\n");
    CHECK(contains("Unexpected arguments.") && heap_test_calls == 0u);
    issue("heaptest\n");
    CHECK(contains("Heap self-test: PASS") && heap_test_calls == 1u);
    heap_test_passes = false;
    issue("heaptest\n");
    CHECK(contains("Heap self-test: FAIL") && heap_test_calls == 2u);
    CHECK(contains("utamo> "));
}

static void test_scheduler_commands(void)
{
    issue("help\n");
    CHECK(contains("ps       List scheduled thread snapshots"));
    CHECK(contains("threads  Alias for ps"));
    CHECK(contains("schedulerstats Scheduler counters"));
    CHECK(contains("schedtest Bounded scheduler"));
    CHECK(contains("sleep    Block this thread"));

    issue("ps extra\n");
    CHECK(contains("Unexpected arguments.") && scheduler_list_calls == 0u);
    issue("threads extra\n");
    CHECK(contains("Unexpected arguments.") && scheduler_list_calls == 0u);
    issue("schedulerstats extra\n");
    CHECK(contains("Unexpected arguments.") && scheduler_stats_calls == 0u);
    issue("schedtest extra\n");
    CHECK(contains("Unexpected arguments.") && scheduler_test_calls == 0u);

    issue("ps\n");
    CHECK(scheduler_list_calls == 1u && listed_capacity == UTAMO_SCHED_MAX_TASKS);
    CHECK(contains("TID STATE NAME\n"));
    CHECK(contains("0 READY idle\n"));
    CHECK(contains("1 RUNNING shell\n"));
    CHECK(contains("42 READY worker%s\n"));
    CHECK(contains("43 BLOCKED waiter\n"));
    CHECK(contains("44 SLEEPING sleeper\n"));
    CHECK(contains("45 ZOMBIE retired\n"));
    issue("threads\n");
    CHECK(scheduler_list_calls == 2u && contains("TID STATE NAME\n"));
    CHECK(contains("42 READY worker%s\n") && contains("45 ZOMBIE retired\n"));

    issue("schedulerstats\n");
    CHECK(contains("Scheduler\nThreads: 6\nCurrent TID: 1\nQuantum ticks: 5"));
    CHECK(contains("Ready threads: 1\nSleeping threads: 1\nBlocked threads: 1\nZombie threads: 1"));
    CHECK(contains("Context switches: 4294967297\nTimer preemptions: 123"));
    CHECK(contains("Threads created: 6\nThreads exited: 3\nThreads reaped: 2"));
    CHECK(contains("Scheduler integrity: OK"));
    scheduler_valid = false;
    issue("schedulerstats\n");
    CHECK(contains("Scheduler integrity: FAILED") &&
          !contains("Scheduler integrity: OK"));
    scheduler_valid = true;
    scheduler_ready = false;
    issue("ps\n");
    CHECK(contains("Scheduler: unavailable") && !contains("TID STATE NAME"));
    issue("schedulerstats\n");
    CHECK(contains("Scheduler: unavailable") && !contains("Context switches:"));
    scheduler_ready = true;
    issue("schedtest\n");
    CHECK(scheduler_test_calls == 1u && contains("Scheduler self-test: PASS"));
    scheduler_test_passes = false;
    issue("schedtest\n");
    CHECK(scheduler_test_calls == 2u && contains("Scheduler self-test: FAIL"));
    CHECK(contains("utamo> "));

    static const char *const invalid[] = {
        "sleep\n", "sleep -1\n", "sleep +1\n", "sleep 0x10\n",
        "sleep 1.0\n", "sleep 1ms\n", "sleep 18446744073709551616\n",
        "sleep 99999999999999999999\n", "sleep 1 extra\n", "sleep 1 2\n"
    };
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        issue(invalid[i]);
        CHECK(contains("Usage: sleep <decimal-ms>") && sleep_calls == 0u);
        CHECK(!contains("Sleep completed."));
    }
    issue("sleep 0\n");
    CHECK(sleep_calls == 1u && slept_milliseconds == 0u);
    CHECK(contains("Sleep completed.") && contains("utamo> "));
    issue("sleep 00123\n");
    CHECK(sleep_calls == 2u && slept_milliseconds == 123u);
    CHECK(contains("Sleep completed."));
    issue("sleep 18446744073709551615\n");
    CHECK(sleep_calls == 3u && slept_milliseconds == UINT64_MAX);
    CHECK(contains("Sleep completed."));
    sleep_passes = false;
    issue("sleep 50\n");
    CHECK(sleep_calls == 4u && slept_milliseconds == 50u);
    CHECK(contains("Sleep failed.") && !contains("Sleep completed."));
    CHECK(contains("utamo> "));
}

static void test_process_commands(void)
{
    issue("help\n");
    CHECK(contains("processes Native user process accounting"));
    CHECK(contains("usertest Bounded Ring 3 isolation and fault tests"));
    issue("processes extra\n");
    CHECK(contains("Unexpected arguments.") && process_stats_calls == 0u);
    issue("usertest extra\n");
    CHECK(contains("Unexpected arguments.") && process_stats_calls == 0u &&
          process_test_calls == 0u);
    issue("processes\n");
    CHECK(process_stats_calls == 1u && contains("Processes\nAvailable: yes"));
    CHECK(contains("Active processes: 3\nProcesses created: 4294967299"));
    CHECK(contains("Processes exited: 2\nProcesses reaped: 1"));
    CHECK(contains("User faults: 17\nSyscalls: 18446744073709551615"));
    CHECK(contains("User timer preemptions: 4294967301"));
    CHECK(contains("Address-space switches: 8589934600"));
    CHECK(contains("utamo> "));
    issue("usertest\n");
    CHECK(contains("User process self-test: PASS") && process_test_calls == 1u);
    CHECK(!contains("User process self-test: FAIL"));
    process_test_passes = false;
    issue("usertest\n");
    CHECK(contains("User process self-test: FAIL") && process_test_calls == 2u);
    CHECK(!contains("User process self-test: PASS"));

    process_status = UTAMO_USER_NO_NX;
    issue("processes\n");
    CHECK(contains("Processes\nAvailable: no") && !contains("Available: yes"));
    issue("usertest\n");
    CHECK(contains("User processes unavailable: NX is required"));
    CHECK(!contains("User process self-test:") && process_test_calls == 2u);
    static const enum arch_user_status unavailable[] = {
        UTAMO_USER_UNINITIALIZED, UTAMO_USER_UNSUPPORTED_CPU,
        UTAMO_USER_UNSUPPORTED_PAGING, UTAMO_USER_BAD_CONTEXT,
        UTAMO_USER_SETUP_FAILED
    };
    for (size_t i = 0u; i < sizeof(unavailable) / sizeof(unavailable[0]); ++i) {
        process_status = unavailable[i];
        issue("usertest\n");
        CHECK(contains("User processes unavailable: unsupported CPU/paging configuration"));
        CHECK(!contains("User process self-test:") && process_test_calls == 2u);
    }
    process_status = UTAMO_USER_READY;
    process_ready = false;
    issue("processes\n");
    CHECK(contains("Process diagnostics unavailable.") && !contains("Active processes:"));
    issue("usertest\n");
    CHECK(contains("Process diagnostics unavailable.") && process_test_calls == 2u);
    CHECK(!contains("User process self-test:"));
    process_ready = true;
    scheduler_ready = false;
    issue("processes\n");
    CHECK(contains("Process diagnostics unavailable.") && !contains("Active processes:"));
    scheduler_ready = true;
    process_test_passes = true;
    issue("usertest\n");
    CHECK(contains("User process self-test: PASS") && process_test_calls == 3u);
    CHECK(contains("utamo> "));
}

static void test_filesystem_commands(void)
{
    issue("help\n");
    CHECK(contains("ls/cat") && contains("exec") && contains("fstest"));
    issue("ls\n");
    CHECK(contains("mock ls /\n"));
    issue("ls /bin\n");
    CHECK(contains("mock ls /bin\n"));
    issue("cat /etc/motd\n");
    CHECK(contains("mock cat /etc/motd\n"));
    issue("cat\n");
    CHECK(contains("Usage:") && !contains("mock cat"));
    issue("cat /etc/motd extra\n");
    CHECK(contains("Unexpected arguments.") && !contains("mock cat"));
    issue("exec /bin/hello one argument\n");
    CHECK(contains("mock exec /bin/hello one argument") && contains("Exec: PASS"));
    issue("exec /missing\n");
    CHECK(contains("Exec: FAIL"));
    issue("fstest\n");
    CHECK(contains("Filesystem self-test: PASS"));
    issue("fstest extra\n");
    CHECK(contains("Unexpected arguments.") && !contains("Filesystem self-test:"));
}

static void test_direct_output_preemption(void)
{
    CHECK(unprotected_direct_io == 0u);
    CHECK(preempt_depth == 0u && preempt_unbalanced == 0u);
    CHECK(preempt_disables == 2u && preempt_enables == 2u);
    /* The shell must restore an already nested nonpreemptible region. */
    preempt_disable();
    issue("clear\n");
    CHECK(preempt_depth == 1u);
    issue("versiox\bn\n");
    CHECK(preempt_depth == 1u && contains("UTAMO OS " UTAMO_VERSION));
    preempt_enable();
    CHECK(preempt_depth == 0u && preempt_unbalanced == 0u);
    CHECK(preempt_disables == preempt_enables);
    CHECK(unprotected_direct_io == 0u);
}

static void test_stop(const char *command, int expected)
{
    const int result = setjmp(stop_target);
    if (result == 0) {
        issue(command);
        CHECK(false); /* A fatal action must not silently return. */
    } else {
        CHECK(result == expected);
        if (expected == 1) {
            CHECK(interrupt_disables == 1u);
            CHECK(contains("System halted."));
            CHECK(!contains("utamo> "));
        }
    }
    /* Simulate a fresh command line after each fatal test double. */
    static struct memory_map restart_map;
    pending_input = NULL;
    shell_init(&restart_map, NULL);
}

int main(void)
{
    test_commands();
    test_memory_commands();
    test_heap_commands();
    test_scheduler_commands();
    test_process_commands();
    test_filesystem_commands();
    test_direct_output_preemption();
    test_stop("halt\n", 1);
    test_stop("fault ud2\n", 2);
    test_stop("fault div0\n", 3);
    test_stop("fault pf\n", 4);
    test_stop("fault vmm\n", 5);
    test_stop("fault stack\n", 6);
    (void)printf("UTAMO shell command host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}

/* Filesystem routing doubles; actual VFS/ELF contracts have separate fixtures. */
bool filesystem_run(const char *path, const char *argument)
{
    kprintf("mock exec %s %s\n", path, argument);
    return strcmp(path, "/bin/hello") == 0;
}
bool filesystem_selftest(void) { return true; }
void filesystem_list(const char *path) { kprintf("mock ls %s\n", path); }
void filesystem_cat(const char *path) { kprintf("mock cat %s\n", path); }

void pci_list(void) { kprintf("PCI mock\n"); }
void storage_status(void) { kprintf("Storage mock\n"); }
bool storage_selftest(void) { return true; }
