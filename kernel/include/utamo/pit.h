/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PIT_H
#define UTAMO_PIT_H
#include <stdint.h>
#define UTAMO_PIT_HZ 100u
#define UTAMO_PIT_INPUT_HZ 1193182u
#define UTAMO_PIT_DIVISOR 11932u
/* Init once, IF=0 and IRQ0 masked. Channel0, mode2, lobyte/hibyte. */
void pit_init(void);
void pit_on_irq(void);
/* Preserves IF; ticks saturate at UINT64_MAX, never move backwards. */
uint64_t pit_get_ticks(void);
/* Nominal 100 Hz conversion; milliseconds saturate rather than overflow. */
uint64_t pit_ticks_to_seconds(uint64_t ticks);
uint64_t pit_ticks_to_milliseconds(uint64_t ticks);
#endif
