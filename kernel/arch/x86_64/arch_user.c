/* SPDX-License-Identifier: MIT */
#include <utamo/arch_user.h>
#include <utamo/cpu.h>
#include <utamo/gdt.h>
#include <utamo/idt.h>
#include <utamo/paging.h>

static enum arch_user_status status = UTAMO_USER_UNINITIALIZED;

static void clear_thread_segments(void)
{
    /* Loading null selectors alone need not clear the hidden 64-bit bases. */
    cpu_user_clear_segments();
    cpu_write_msr(UTAMO_FS_BASE_MSR, 0u);
    cpu_write_msr(UTAMO_GS_BASE_MSR, 0u);
    cpu_write_msr(UTAMO_KERNEL_GS_BASE_MSR, 0u);
}

enum arch_user_status arch_user_get_status(void)
{
    return status;
}

bool arch_user_ready(void)
{
    return status == UTAMO_USER_READY;
}

enum arch_user_status arch_user_init(void)
{
    const uint64_t saved = cpu_irq_save();
    if ((saved & UINT64_C(0x200)) != 0u) {
        cpu_irq_restore(saved);
        return UTAMO_USER_BAD_CONTEXT;
    }
    if (arch_user_ready()) {
        cpu_irq_restore(saved);
        return status;
    }
    struct cpu_cpuid_result basic, extended, features;
    cpu_cpuid(0u, 0u, &basic);
    cpu_cpuid(UINT32_C(0x80000000), 0u, &extended);
    if (basic.eax < 1u || extended.eax < UINT32_C(0x80000001)) {
        status = UTAMO_USER_UNSUPPORTED_CPU;
        cpu_irq_restore(saved);
        return status;
    }
    cpu_cpuid(1u, 0u, &basic);
    cpu_cpuid(UINT32_C(0x80000001), 0u, &features);
    if ((basic.edx & (UINT32_C(1) << 5u)) == 0u ||
        (features.edx & (UINT32_C(1) << 29u)) == 0u) {
        status = UTAMO_USER_UNSUPPORTED_CPU;
        cpu_irq_restore(saved);
        return status;
    }
    if ((features.edx & (UINT32_C(1) << 20u)) == 0u) {
        status = UTAMO_USER_NO_NX;
        cpu_irq_restore(saved);
        return status;
    }
    const uint64_t cr0 = cpu_read_cr0();
    const uint64_t cr4 = cpu_read_cr4();
    const uint64_t efer = cpu_read_msr(UTAMO_EFER_MSR);
    if ((efer & UTAMO_EFER_NXE) == 0u) {
        status = UTAMO_USER_NO_NX;
        cpu_irq_restore(saved);
        return status;
    }
    const uint64_t required_cr0 = UTAMO_CR0_PE | UTAMO_CR0_PG | UTAMO_CR0_WP;
    const uint64_t required_efer = UTAMO_EFER_LME | UTAMO_EFER_LMA;
    if ((cr0 & required_cr0) != required_cr0 ||
        (cr4 & UTAMO_CR4_PAE) == 0u ||
        (efer & required_efer) != required_efer ||
        (cr4 & (UTAMO_CR4_LA57 | UTAMO_CR4_PCIDE |
                UTAMO_CR4_PKE | UTAMO_CR4_CET)) != 0u) {
        status = UTAMO_USER_UNSUPPORTED_PAGING;
        cpu_irq_restore(saved);
        return status;
    }
    const bool sysenter = (basic.edx & (UINT32_C(1) << 11u)) != 0u;
    /* INT128 is the only supported user entry. Never trust inherited fast
     * syscall MSRs, and never access SYSENTER MSRs without CPUID.SEP. */
    cpu_write_msr(UTAMO_EFER_MSR, efer & ~UTAMO_EFER_SCE);
    if (sysenter) {
        cpu_write_msr(UTAMO_SYSENTER_CS_MSR, 0u);
    }
    cpu_write_cr4(cr4 & ~UTAMO_CR4_FSGSBASE);
    cpu_write_cr0(cr0 | UTAMO_CR0_TS);
    clear_thread_segments();
    if (cpu_read_cr0() != (cr0 | UTAMO_CR0_TS) ||
        cpu_read_cr4() != (cr4 & ~UTAMO_CR4_FSGSBASE) ||
        cpu_read_msr(UTAMO_EFER_MSR) != (efer & ~UTAMO_EFER_SCE) ||
        (sysenter && cpu_read_msr(UTAMO_SYSENTER_CS_MSR) != 0u) ||
        cpu_read_msr(UTAMO_FS_BASE_MSR) != 0u ||
        cpu_read_msr(UTAMO_GS_BASE_MSR) != 0u ||
        cpu_read_msr(UTAMO_KERNEL_GS_BASE_MSR) != 0u ||
        !idt_enable_user_syscall()) {
        status = UTAMO_USER_SETUP_FAILED;
        cpu_irq_restore(saved);
        return status;
    }
    status = UTAMO_USER_READY;
    cpu_irq_restore(saved);
    return status;
}

bool arch_user_prepare_return(uint64_t kernel_stack_top)
{
    const uint64_t saved = cpu_irq_save();
    if ((saved & UINT64_C(0x200)) != 0u || !arch_user_ready() ||
        !gdt_set_rsp0(kernel_stack_top)) {
        cpu_irq_restore(saved);
        return false;
    }
    clear_thread_segments();
    /* No user can execute CLTS; nevertheless establish the policy at entry. */
    const uint64_t cr0 = cpu_read_cr0();
    if ((cr0 & UTAMO_CR0_TS) == 0u) {
        cpu_write_cr0(cr0 | UTAMO_CR0_TS);
    }
    cpu_irq_restore(saved);
    return true;
}
