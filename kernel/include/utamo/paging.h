/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PAGING_H
#define UTAMO_PAGING_H
#include <stdint.h>
struct cpu_cpuid_result { uint32_t eax, ebx, ecx, edx; };
void cpu_cpuid(uint32_t leaf, uint32_t subleaf, struct cpu_cpuid_result *out);
uint64_t cpu_read_cr0(void);
void cpu_write_cr0(uint64_t value);
uint64_t cpu_read_cr3(void);
void cpu_write_cr3(uint64_t value);
uint64_t cpu_read_cr4(void);
uint64_t cpu_read_msr(uint32_t index);
void cpu_write_msr(uint32_t index, uint64_t value);
void cpu_invlpg(uint64_t virt);
/* Explicit diagnostic memory operations, including fatal probes. */
void memory_write_address(uint64_t virt, uint64_t value);
void memory_execute_address(uint64_t virt);
#define UTAMO_EFER_MSR UINT32_C(0xc0000080)
#define UTAMO_EFER_NXE (UINT64_C(1) << 11u)
#define UTAMO_CR0_WP (UINT64_C(1) << 16u)
#define UTAMO_CR4_LA57 (UINT64_C(1) << 12u)
#endif
