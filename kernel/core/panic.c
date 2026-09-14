/* SPDX-License-Identifier: MIT */
#include <stdbool.h>
#include <utamo/cpu.h>
#include <utamo/log.h>
#include <utamo/panic.h>
#include <utamo/serial.h>

static bool panic_active;

_Noreturn void kernel_panic(const char *message, const char *file,
                          unsigned int line)
{
    cpu_disable_interrupts();
    if (panic_active) {
        (void)serial_write_string("\nUTAMO: recursive panic\n");
        cpu_halt();
    }
    panic_active = true;
    kprintf("\n*** UTAMO KERNEL PANIC ***\n");
    kprintf("%s\nLocation: %s:%u\n", message, file, line);
    kprintf("Unsafe continuation prevented. CPU halted.\n");
    cpu_halt();
}
