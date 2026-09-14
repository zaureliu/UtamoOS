/* SPDX-License-Identifier: MIT */
#include <utamo/cpu.h>
#include <utamo/io.h>
#include <utamo/pit.h>
/* One writer (IRQ0). Main takes an IF-preserving snapshot on the single BSP.
 * Volatile exposes asynchronous changes; IRQ exclusion supplies ownership. */
static volatile uint64_t ticks;
void pit_init(void)
{
    ticks = 0;
    io_out8(0x43, 0x34);
    io_out8(0x40, (uint8_t)(UTAMO_PIT_DIVISOR & 0xffu));
    io_out8(0x40, (uint8_t)(UTAMO_PIT_DIVISOR >> 8u));
}
void pit_on_irq(void)
{
    if (ticks != UINT64_MAX) {
        ++ticks;
    }
}
uint64_t pit_get_ticks(void)
{
    const uint64_t flags = cpu_irq_save();
    const uint64_t snapshot = ticks;
    cpu_irq_restore(flags);
    return snapshot;
}
