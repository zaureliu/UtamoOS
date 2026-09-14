/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_MEMORY_SELFTEST_H
#define UTAMO_MEMORY_SELFTEST_H
#include <stdbool.h>
bool memory_pmm_selftest(void);
bool memory_vmm_selftest(void);
_Noreturn void memory_fault_unmapped(void);
_Noreturn void memory_fault_readonly(void);
_Noreturn void memory_fault_nx(void);
#endif
