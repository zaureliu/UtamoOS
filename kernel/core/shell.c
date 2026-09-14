/* SPDX-License-Identifier: MIT */
#include <utamo/shell.h>

#include <utamo/cpu.h>
#include <utamo/interrupts.h>
#include <utamo/keyboard.h>
#include <utamo/log.h>
#include <utamo/pit.h>
#include <utamo/serial.h>
#include <utamo/shell_line.h>
#include <utamo/string.h>
#include <utamo/version.h>

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

static void show_memory(void)
{
    kprintf("Memory map entries: %llu\nUsable memory: %llu MiB (%llu bytes)\n",
            (unsigned long long)system_memory->count,
            (unsigned long long)(system_memory->usable_bytes / (1024u * 1024u)),
            (unsigned long long)system_memory->usable_bytes);
    kprintf("Bootloader reclaimable: %llu KiB\n",
            (unsigned long long)(system_memory->bootloader_reclaimable_bytes /
                                  1024u));
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
        kprintf("Usage: fault ud2|div0|pf (fatal; restart QEMU afterwards)\n");
        return;
    }
    if (strcmp(kind, "ud2") == 0) {
        exception_fault_ud2();
    } else if (strcmp(kind, "div0") == 0) {
        exception_fault_div0();
    } else if (strcmp(kind, "pf") == 0) {
        exception_fault_page();
    } else {
        kprintf("Usage: fault ud2|div0|pf (fatal; restart QEMU afterwards)\n");
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
    if (*command.arguments != '\0') {
        kprintf("Unexpected arguments. Type help.\n");
        return;
    }
    if (strcmp(name, "help") == 0) {
        kprintf("help     List implemented commands\n");
        kprintf("clear    Clear framebuffer and serial terminal\n");
        kprintf("version  Kernel version\n");
        kprintf("sysinfo  Known boot and hardware information\n");
        kprintf("mem      Boot memory map totals\n");
        kprintf("uptime   PIT uptime and ticks\n");
        kprintf("echo     Repeat following text\n");
        kprintf("halt     Disable interrupts and stop CPU\n");
        kprintf("fault    ud2, div0 or pf: fatal exception self-test\n");
    } else if (strcmp(name, "clear") == 0) {
        terminal_clear(system_terminal);
        /* ANSI is for the external serial terminal, not the bitmap renderer. */
        (void)serial_write_string("\x1b[2J\x1b[H");
    } else if (strcmp(name, "version") == 0) {
        kprintf("UTAMO OS %s\n", (const char *)UTAMO_VERSION);
    } else if (strcmp(name, "sysinfo") == 0) {
        show_sysinfo();
    } else if (strcmp(name, "mem") == 0) {
        show_memory();
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
            terminal_putc(system_terminal, '\b');
            (void)serial_write_string("\b \b");
        } else if (action == UTAMO_SHELL_SUBMITTED) {
            kprintf("\n");
            execute_line();
            show_prompt();
        }
    }
}
