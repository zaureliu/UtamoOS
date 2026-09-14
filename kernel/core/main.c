/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <utamo/boot.h>
#include <utamo/cpu.h>
#include <utamo/kernel.h>
#include <utamo/gdt.h>
#include <utamo/heap.h>
#include <utamo/idt.h>
#include <utamo/pic.h>
#include <utamo/pit.h>
#include <utamo/keyboard.h>
#include <utamo/shell.h>
#include <utamo/scheduler.h>
#include <utamo/interrupts.h>
#include <utamo/log.h>
#include <utamo/panic.h>
#include <utamo/memory.h>
#include <utamo/serial.h>
#include <utamo/terminal.h>
#include <utamo/version.h>

/* Boot-owned state lives for the entire single-BSP kernel lifetime. */
static struct framebuffer boot_framebuffer;
static struct terminal boot_terminal;
static struct memory_map boot_memory;

_Noreturn void kernel_main(void)
{
    cpu_disable_interrupts();
    const bool serial_available = serial_init();
    if (serial_available && !log_add_sink(serial_sink, NULL)) {
        (void)serial_write_string("UTAMO: cannot register serial logger\n");
        cpu_halt();
    }
    LOG_INFO("Starting UTAMO kernel");
    if (!boot_protocol_supported()) {
        PANIC("Limine base protocol revision 3 is required");
    }
    if (!boot_paging_supported()) {
        PANIC("Limine 4-level paging response is required");
    }
    if (!boot_init_framebuffer(&boot_framebuffer)) {
        PANIC("No supported RGB framebuffer (24/32 bpp)");
    }
    if (!terminal_init(&boot_terminal, &boot_framebuffer)) {
        PANIC("Framebuffer is too small for the terminal");
    }
    if (!log_add_sink(terminal_sink, &boot_terminal)) {
        PANIC("Cannot register terminal logger");
    }
    kprintf("==============================================\n");
    kprintf("UTAMO OS\nExperimental x86_64 Operating System\n\n");
    kprintf("Version: %s\nArchitecture: x86_64\n\n", (const char *)UTAMO_VERSION);
    LOG_OK("Limine boot protocol (base revision 3)");
    LOG_OK("Kernel loaded: %s", (const char *)UTAMO_KERNEL_NAME);
    LOG_OK("Framebuffer detected: %llux%llu, %u bpp",
           (unsigned long long)boot_framebuffer.width,
           (unsigned long long)boot_framebuffer.height,
           (unsigned int)(boot_framebuffer.bytes_per_pixel * 8u));
    LOG_OK("Terminal initialized");
    if (serial_available && serial_is_ready()) {
        LOG_OK("Serial COM1 initialized (115200 8N1)");
    } else {
        LOG_WARN("Serial COM1 unavailable or timed out");
    }
    if (!boot_read_memory_map(&boot_memory)) {
        PANIC("Invalid, missing or oversized Limine memory map");
    }
    LOG_INFO("Memory map entries: %llu", (unsigned long long)boot_memory.count);
    kprintf("Total usable memory: %llu MiB\n",
            (unsigned long long)(boot_memory.usable_bytes / (1024u * 1024u)));
    gdt_init();
    LOG_OK("GDT initialized");
    exception_set_terminal(&boot_terminal);
    if (!idt_init()) {
        PANIC("Cannot initialize IDT");
    }
    LOG_OK("IDT initialized");
    LOG_OK("CPU exception handlers initialized");
    if (!memory_init(&boot_memory, &boot_framebuffer)) {
        PANIC("Cannot initialize physical/virtual memory safely");
    }
    if (!heap_init()) {
        PANIC("Cannot initialize kernel heap");
    }
    LOG_OK("Kernel heap initialized");
    pic_init();
    LOG_OK("PIC initialized");
    pit_init();
    LOG_OK("PIT timer initialized (100 Hz)");
    if (!scheduler_init()) {
        PANIC("Cannot initialize kernel scheduler");
    }
    LOG_OK("Kernel scheduler initialized (round-robin, 2 ticks)");
    if (!keyboard_init()) {
        PANIC("Cannot initialize PS/2 keyboard");
    }
    LOG_OK("PS/2 keyboard initialized");
    pic_unmask(0);
    pic_unmask(1);
    cpu_enable_interrupts();
    LOG_OK("Interrupts enabled");
    kprintf("\nUTAMO OS ready.\n\n");
    shell_init(&boot_memory, &boot_terminal);
    for (;;) {
        thread_reap();
        shell_process_input();
        scheduler_wait_input();
    }
}
