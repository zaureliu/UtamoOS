/* SPDX-License-Identifier: MIT */
/* Host-only shell integration: hardware effects are explicit test doubles. */
#include <utamo/shell.h>

#include <setjmp.h>
#include <stdio.h>
#include <utamo/cpu.h>
#include <utamo/interrupts.h>
#include <utamo/keyboard.h>
#include <utamo/log.h>
#include <utamo/pit.h>
#include <utamo/serial.h>
#include <utamo/string.h>
#include <utamo/version.h>

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
    ++clears;
}

void terminal_putc(struct terminal *terminal, char character)
{
    CHECK(terminal != NULL && terminal->initialized && character == '\b');
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
    test_stop("halt\n", 1);
    test_stop("fault ud2\n", 2);
    test_stop("fault div0\n", 3);
    test_stop("fault pf\n", 4);
    (void)printf("UTAMO shell command host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
