/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_CPU_H
#define UTAMO_CPU_H

#include <stdint.h>

void cpu_disable_interrupts(void);
void cpu_enable_interrupts(void);
/* Short single-BSP critical sections. Restore only the saved IF state. */
uint64_t cpu_irq_save(void);
void cpu_irq_restore(uint64_t flags);
/* Call with IF=0 after testing for work: STI shadow closes the idle race. */
void cpu_wait_interrupt(void);
/* Permanent stop on the current CPU. Never enables interrupts or returns. */
_Noreturn void cpu_halt(void);

#endif
