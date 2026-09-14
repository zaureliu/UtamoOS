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
void cpu_write_cr4(uint64_t value);
uint64_t cpu_read_msr(uint32_t index);
void cpu_write_msr(uint32_t index, uint64_t value);
void cpu_invlpg(uint64_t virt);
/* Explicit diagnostic memory operations, including fatal probes. */
void memory_write_address(uint64_t virt, uint64_t value);
void memory_execute_address(uint64_t virt);
#define UTAMO_EFER_MSR UINT32_C(0xc0000080)
#define UTAMO_FS_BASE_MSR UINT32_C(0xc0000100)
#define UTAMO_GS_BASE_MSR UINT32_C(0xc0000101)
#define UTAMO_KERNEL_GS_BASE_MSR UINT32_C(0xc0000102)
#define UTAMO_SYSENTER_CS_MSR UINT32_C(0x174)
#define UTAMO_EFER_SCE UINT64_C(1)
#define UTAMO_EFER_LME (UINT64_C(1) << 8u)
#define UTAMO_EFER_LMA (UINT64_C(1) << 10u)
#define UTAMO_CR0_PE UINT64_C(1)
#define UTAMO_CR0_TS (UINT64_C(1) << 3u)
#define UTAMO_CR0_PG (UINT64_C(1) << 31u)
#define UTAMO_CR4_PAE (UINT64_C(1) << 5u)
#define UTAMO_CR4_FSGSBASE (UINT64_C(1) << 16u)
#define UTAMO_CR4_PCIDE (UINT64_C(1) << 17u)
#define UTAMO_CR4_PKE (UINT64_C(1) << 22u)
#define UTAMO_CR4_CET (UINT64_C(1) << 23u)
#define UTAMO_EFER_NXE (UINT64_C(1) << 11u)
#define UTAMO_CR0_WP (UINT64_C(1) << 16u)
#define UTAMO_CR4_LA57 (UINT64_C(1) << 12u)
#endif
