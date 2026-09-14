/* SPDX-License-Identifier: MIT */
/* Host-only hardware simulation; no privileged instruction is executed. */
#include <stdio.h>
#include <string.h>
#include <utamo/arch_user.h>
#include <utamo/cpu.h>
#include <utamo/gdt.h>
#include <utamo/idt.h>
#include <utamo/paging.h>

static unsigned int checks, failures;
#define CHECK(v) do { ++checks; if (!(v)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #v); } } while (0)
static bool callbacks_valid = true;
#define OBSERVE(v) do { if (!(v)) { callbacks_valid = false; \
    (void)printf("Invalid mock call line %u: %s\n", (unsigned int)__LINE__, #v); } } while (0)
enum register_id { R_CR0, R_CR4, R_EFER, R_SYSENTER, R_FS, R_GS, R_KGS, R_COUNT };
static struct {
    uint64_t flags, regs[R_COUNT];
    uint32_t max_basic, max_extended, basic_edx, extended_edx;
    unsigned int writes, msr_reads, sep_reads, sep_writes, segment_clears;
    enum register_id ignored_write;
} hw;
static struct tss64 *loaded_tss;
static struct idt_gate *loaded_idt;
const int32_t interrupt_stub_table[UTAMO_IDT_ENTRIES] = {0};
void idt_load(const struct descriptor_pointer *pointer);

static void reset_cpu(void)
{
    memset(&hw, 0, sizeof(hw));
    hw.flags = 2u;
    hw.max_basic = 7u;
    hw.max_extended = UINT32_C(0x80000008);
    hw.basic_edx = (UINT32_C(1) << 5u) | (UINT32_C(1) << 11u);
    hw.extended_edx = (UINT32_C(1) << 29u) | (UINT32_C(1) << 20u);
    hw.regs[R_CR0] = UTAMO_CR0_PE | UTAMO_CR0_PG | UTAMO_CR0_WP | UINT64_C(0x20);
    hw.regs[R_CR4] = UTAMO_CR4_PAE | UTAMO_CR4_FSGSBASE | UINT64_C(0x200);
    hw.regs[R_EFER] = UTAMO_EFER_LME | UTAMO_EFER_LMA |
                      UTAMO_EFER_NXE | UTAMO_EFER_SCE;
    hw.regs[R_SYSENTER] = 8u;
    hw.regs[R_FS] = 123u;
    hw.regs[R_GS] = 456u;
    hw.regs[R_KGS] = 789u;
    hw.ignored_write = R_COUNT;
    callbacks_valid = true;
}

uint64_t cpu_irq_save(void)
{
    const uint64_t flags = hw.flags;
    hw.flags &= ~UINT64_C(0x200);
    return flags;
}
void cpu_irq_restore(uint64_t flags)
{
    hw.flags = (hw.flags & ~UINT64_C(0x200)) | (flags & UINT64_C(0x200));
}
void cpu_cpuid(uint32_t leaf, uint32_t subleaf, struct cpu_cpuid_result *out)
{
    OBSERVE(subleaf == 0u && out != NULL);
    *out = (struct cpu_cpuid_result){0};
    switch (leaf) {
    case 0u: out->eax = hw.max_basic; break;
    case 1u: out->edx = hw.basic_edx; break;
    case UINT32_C(0x80000000): out->eax = hw.max_extended; break;
    case UINT32_C(0x80000001): out->edx = hw.extended_edx; break;
    default: OBSERVE(false); break;
    }
}
static void write_register(enum register_id reg, uint64_t value)
{
    OBSERVE((hw.flags & UINT64_C(0x200)) == 0u);
    ++hw.writes;
    if (reg != hw.ignored_write) {
        hw.regs[reg] = value;
    }
}
uint64_t cpu_read_cr0(void) { return hw.regs[R_CR0]; }
uint64_t cpu_read_cr4(void) { return hw.regs[R_CR4]; }
void cpu_write_cr0(uint64_t value) { write_register(R_CR0, value); }
void cpu_write_cr4(uint64_t value) { write_register(R_CR4, value); }
static enum register_id msr_register(uint32_t index)
{
    OBSERVE((hw.basic_edx & (UINT32_C(1) << 5u)) != 0u);
    OBSERVE((hw.extended_edx & (UINT32_C(1) << 29u)) != 0u);
    switch (index) {
    case UTAMO_EFER_MSR: return R_EFER;
    case UTAMO_SYSENTER_CS_MSR:
        OBSERVE((hw.basic_edx & (UINT32_C(1) << 11u)) != 0u);
        return R_SYSENTER;
    case UTAMO_FS_BASE_MSR: return R_FS;
    case UTAMO_GS_BASE_MSR: return R_GS;
    case UTAMO_KERNEL_GS_BASE_MSR: return R_KGS;
    default: OBSERVE(false); return R_EFER;
    }
}
uint64_t cpu_read_msr(uint32_t index)
{
    ++hw.msr_reads;
    if (index == UTAMO_SYSENTER_CS_MSR) { ++hw.sep_reads; }
    return hw.regs[msr_register(index)];
}
void cpu_write_msr(uint32_t index, uint64_t value)
{
    if (index == UTAMO_SYSENTER_CS_MSR) { ++hw.sep_writes; }
    write_register(msr_register(index), value);
}
void cpu_user_clear_segments(void)
{
    OBSERVE((hw.flags & UINT64_C(0x200)) == 0u);
    ++hw.segment_clears;
}
void gdt_load(const struct descriptor_pointer *pointer)
{
    OBSERVE(pointer != NULL && pointer->limit == 55u);
    const uint64_t *const entries = (const uint64_t *)(uintptr_t)pointer->base;
    const uint64_t address = ((entries[5] >> 16u) & UINT64_C(0xffffff)) |
        ((entries[5] >> 56u) << 24u) | (entries[6] << 32u);
    loaded_tss = (struct tss64 *)(uintptr_t)address;
}
void idt_load(const struct descriptor_pointer *pointer)
{
    OBSERVE(pointer != NULL && pointer->limit == 4095u);
    loaded_idt = (struct idt_gate *)(uintptr_t)pointer->base;
}

static void test_descriptors(void)
{
    reset_cpu();
    CHECK(!gdt_set_rsp0(UINT64_C(0xffffc00040011000)));
    CHECK(!idt_enable_user_syscall());
    gdt_init();
    CHECK(loaded_tss != NULL);
    CHECK(loaded_tss->iomap_base == 104u);
    CHECK(loaded_tss->rsp[0] == 0u);
    const struct tss64 original = *loaded_tss;
    const uint64_t good = UINT64_C(0xffffc00040011000);
    CHECK(gdt_set_rsp0(good));
    CHECK(loaded_tss->rsp[0] == good);
    struct tss64 expected = original;
    expected.rsp[0] = good;
    CHECK(memcmp(loaded_tss, &expected, sizeof(expected)) == 0);
    const uint64_t invalid[] = {
        0u, UINT64_C(0x70000000), UINT64_C(0x0000800000000000),
        UINT64_C(0xffff7ffffffff000), UINT64_C(0xffff800000000000), good + 1u
    };
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(!gdt_set_rsp0(invalid[i]));
        CHECK(memcmp(loaded_tss, &expected, sizeof(expected)) == 0);
    }
    hw.flags |= UINT64_C(0x200);
    CHECK(!gdt_set_rsp0(good + 4096u));
    CHECK((hw.flags & UINT64_C(0x200)) != 0u);
    CHECK(loaded_tss->rsp[0] == good);
    hw.flags &= ~UINT64_C(0x200);
    CHECK(idt_init());
    CHECK(loaded_idt != NULL);
    bool all_default = true;
    for (size_t i = 0u; i < UTAMO_IDT_ENTRIES; ++i) {
        all_default = all_default && loaded_idt[i].type_attributes == 0x8eu;
    }
    CHECK(all_default);
    struct idt_gate gate;
    CHECK(idt_user_gate_encode(&gate, UINT64_C(0xffffffff12345678)));
    const unsigned char expected_bytes[16] = {
        0x78u, 0x56u, 0x08u, 0x00u, 0x00u, 0xeeu, 0x34u, 0x12u,
        0xffu, 0xffu, 0xffu, 0xffu, 0x00u, 0x00u, 0x00u, 0x00u
    };
    CHECK(memcmp(&gate, expected_bytes, sizeof(gate)) == 0);
    const struct idt_gate before = gate;
    CHECK(!idt_user_gate_encode(NULL, 1u));
    CHECK(!idt_user_gate_encode(&gate, 0u));
    CHECK(!idt_user_gate_encode(&gate, UINT64_C(0x0000800000000000)));
    CHECK(memcmp(&gate, &before, sizeof(gate)) == 0);
    hw.flags |= UINT64_C(0x200);
    CHECK(!idt_enable_user_syscall());
    CHECK(loaded_idt[128].type_attributes == 0x8eu);
    CHECK((hw.flags & UINT64_C(0x200)) != 0u);
    CHECK(callbacks_valid);
}

static void expect_preflight(enum arch_user_status expected)
{
    uint64_t before[R_COUNT];
    memcpy(before, hw.regs, sizeof(before));
    CHECK(arch_user_init() == expected);
    CHECK(arch_user_get_status() == expected);
    CHECK(!arch_user_ready());
    CHECK(hw.writes == 0u && hw.segment_clears == 0u);
    CHECK(memcmp(before, hw.regs, sizeof(before)) == 0);
    CHECK(loaded_idt[128].type_attributes == 0x8eu);
    CHECK((hw.flags & UINT64_C(0x200)) == 0u);
    CHECK(callbacks_valid);
}

static void test_preconditions(void)
{
    reset_cpu();
    CHECK(arch_user_get_status() == UTAMO_USER_UNINITIALIZED);
    CHECK(!arch_user_prepare_return(UINT64_C(0xffffc00040011000)));
    CHECK(hw.writes == 0u);
    hw.flags |= UINT64_C(0x200);
    CHECK(arch_user_init() == UTAMO_USER_BAD_CONTEXT);
    CHECK(arch_user_get_status() == UTAMO_USER_UNINITIALIZED);
    CHECK((hw.flags & UINT64_C(0x200)) != 0u);
    CHECK(hw.writes == 0u && hw.msr_reads == 0u);
    reset_cpu(); hw.max_basic = 0u;
    expect_preflight(UTAMO_USER_UNSUPPORTED_CPU);
    CHECK(hw.msr_reads == 0u);
    reset_cpu(); hw.max_extended = UINT32_C(0x80000000);
    expect_preflight(UTAMO_USER_UNSUPPORTED_CPU);
    CHECK(hw.msr_reads == 0u);
    reset_cpu(); hw.basic_edx &= ~(UINT32_C(1) << 5u);
    expect_preflight(UTAMO_USER_UNSUPPORTED_CPU);
    CHECK(hw.msr_reads == 0u);
    reset_cpu(); hw.extended_edx &= ~(UINT32_C(1) << 29u);
    expect_preflight(UTAMO_USER_UNSUPPORTED_CPU);
    CHECK(hw.msr_reads == 0u);
    reset_cpu(); hw.extended_edx &= ~(UINT32_C(1) << 20u);
    expect_preflight(UTAMO_USER_NO_NX);
    CHECK(hw.msr_reads == 0u);
    reset_cpu(); hw.regs[R_EFER] &= ~UTAMO_EFER_NXE;
    expect_preflight(UTAMO_USER_NO_NX);
    const uint64_t required_cr0[] = {UTAMO_CR0_PE, UTAMO_CR0_PG, UTAMO_CR0_WP};
    for (size_t i = 0u; i < sizeof(required_cr0) / sizeof(required_cr0[0]); ++i) {
        reset_cpu(); hw.regs[R_CR0] &= ~required_cr0[i];
        expect_preflight(UTAMO_USER_UNSUPPORTED_PAGING);
    }
    const uint64_t forbidden_cr4[] = {
        UTAMO_CR4_LA57, UTAMO_CR4_PCIDE, UTAMO_CR4_PKE, UTAMO_CR4_CET
    };
    for (size_t i = 0u; i < sizeof(forbidden_cr4) / sizeof(forbidden_cr4[0]); ++i) {
        reset_cpu(); hw.regs[R_CR4] |= forbidden_cr4[i];
        expect_preflight(UTAMO_USER_UNSUPPORTED_PAGING);
    }
    reset_cpu(); hw.regs[R_CR4] &= ~UTAMO_CR4_PAE;
    expect_preflight(UTAMO_USER_UNSUPPORTED_PAGING);
    reset_cpu(); hw.regs[R_EFER] &= ~UTAMO_EFER_LME;
    expect_preflight(UTAMO_USER_UNSUPPORTED_PAGING);
    reset_cpu(); hw.regs[R_EFER] &= ~UTAMO_EFER_LMA;
    expect_preflight(UTAMO_USER_UNSUPPORTED_PAGING);
}

static void test_failed_readback(void)
{
    for (enum register_id reg = R_CR0; reg < R_COUNT; ++reg) {
        reset_cpu();
        hw.ignored_write = reg;
        CHECK(arch_user_init() == UTAMO_USER_SETUP_FAILED);
        CHECK(!arch_user_ready());
        CHECK(loaded_idt[128].type_attributes == 0x8eu);
        CHECK(hw.regs[reg] != 0u);
        CHECK(callbacks_valid);
    }
    /* SEP absent must not touch optional SYSENTER MSRs, even on a later
     * failed readback. A successful retry below verifies the complete policy. */
    reset_cpu();
    hw.basic_edx &= ~(UINT32_C(1) << 11u);
    hw.ignored_write = R_KGS;
    CHECK(arch_user_init() == UTAMO_USER_SETUP_FAILED);
    CHECK(hw.sep_reads == 0u && hw.sep_writes == 0u);
    CHECK(callbacks_valid);
}

static void test_success(void)
{
    reset_cpu();
    const uint64_t old_cr0 = hw.regs[R_CR0];
    const uint64_t old_cr4 = hw.regs[R_CR4];
    const uint64_t old_efer = hw.regs[R_EFER];
    struct idt_gate gates[UTAMO_IDT_ENTRIES];
    memcpy(gates, loaded_idt, sizeof(gates));
    CHECK(arch_user_init() == UTAMO_USER_READY);
    CHECK(arch_user_ready());
    CHECK(arch_user_get_status() == UTAMO_USER_READY);
    CHECK(hw.regs[R_CR0] == (old_cr0 | UTAMO_CR0_TS));
    CHECK(hw.regs[R_CR4] == (old_cr4 & ~UTAMO_CR4_FSGSBASE));
    CHECK(hw.regs[R_EFER] == (old_efer & ~UTAMO_EFER_SCE));
    CHECK(hw.regs[R_SYSENTER] == 0u);
    CHECK(hw.regs[R_FS] == 0u && hw.regs[R_GS] == 0u && hw.regs[R_KGS] == 0u);
    CHECK(hw.sep_reads == 1u && hw.sep_writes == 1u);
    CHECK(hw.segment_clears == 1u);
    CHECK(loaded_idt[128].type_attributes == 0xeeu);
    CHECK(loaded_idt[128].ist == 0u && loaded_idt[128].selector == 8u);
    CHECK(loaded_idt[240].type_attributes == 0x8eu);
    bool other_gates_unchanged = true;
    for (size_t i = 0u; i < UTAMO_IDT_ENTRIES; ++i) {
        if (i != 128u) {
            other_gates_unchanged = other_gates_unchanged &&
                memcmp(&gates[i], &loaded_idt[i], sizeof(gates[i])) == 0;
        }
    }
    CHECK(other_gates_unchanged);
    const unsigned int writes = hw.writes;
    CHECK(arch_user_init() == UTAMO_USER_READY);
    CHECK(hw.writes == writes);
    CHECK(!arch_user_prepare_return(0u));
    CHECK(hw.writes == writes);
    hw.flags |= UINT64_C(0x200);
    CHECK(!arch_user_prepare_return(UINT64_C(0xffffc00040011000)));
    CHECK(arch_user_init() == UTAMO_USER_BAD_CONTEXT);
    CHECK(arch_user_ready());
    CHECK((hw.flags & UINT64_C(0x200)) != 0u);
    CHECK(hw.writes == writes);
    hw.flags &= ~UINT64_C(0x200);
    hw.regs[R_FS] = UINT64_C(0x1000);
    hw.regs[R_GS] = UINT64_C(0x2000);
    hw.regs[R_KGS] = UINT64_C(0x3000);
    hw.regs[R_CR0] &= ~UTAMO_CR0_TS;
    CHECK(arch_user_prepare_return(UINT64_C(0xffffc00040022000)));
    CHECK(loaded_tss->rsp[0] == UINT64_C(0xffffc00040022000));
    CHECK(hw.regs[R_FS] == 0u && hw.regs[R_GS] == 0u && hw.regs[R_KGS] == 0u);
    CHECK((hw.regs[R_CR0] & UTAMO_CR0_TS) != 0u);
    CHECK(hw.segment_clears == 2u);
    CHECK((hw.flags & UINT64_C(0x200)) == 0u);
    CHECK(callbacks_valid);
}

int main(void)
{
    test_descriptors();
    test_preconditions();
    test_failed_readback();
    test_success();
    (void)printf("UTAMO user architecture tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
