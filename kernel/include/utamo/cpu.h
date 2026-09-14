/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_CPU_H
#define UTAMO_CPU_H

void cpu_disable_interrupts(void);
/* Permanent stop on the current CPU. Never enables interrupts or returns. */
_Noreturn void cpu_halt(void);

#endif
