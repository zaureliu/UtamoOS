/* SPDX-License-Identifier: MIT */
/* Host-only: architecture encoders and diagnostics, no privileged instructions. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <utamo/idt.h>
#include <utamo/interrupts.h>
#include <utamo/vmm.h>

static unsigned int checks;
static unsigned int failures;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

static void test_idt_layout(void)
{
    struct idt_gate gate;
    CHECK(idt_gate_encode(&gate, UINT64_C(0xffffffff12345678), 0x08u, 1u));
    /* Independent little-endian architectural byte layout. */
    static const uint8_t expected[16] = {
        0x78u, 0x56u, 0x08u, 0x00u, 0x01u, 0x8eu, 0x34u, 0x12u,
        0xffu, 0xffu, 0xffu, 0xffu, 0x00u, 0x00u, 0x00u, 0x00u
    };
    CHECK(sizeof(gate) == sizeof(expected));
    CHECK(memcmp(&gate, expected, sizeof(expected)) == 0);
    CHECK(idt_gate_encode(&gate, UINT64_C(0x00007fffffffffff), 0x10u, 7u));
    CHECK(gate.offset_low == 0xffffu);
    CHECK(gate.offset_middle == 0xffffu);
    CHECK(gate.offset_high == 0x00007fffu);
    CHECK(gate.selector == 0x10u && gate.ist == 7u);
    CHECK(idt_gate_encode(&gate, UINT64_C(0xffff800000000000), 0x08u, 0u));
    CHECK(gate.offset_low == 0u && gate.offset_middle == 0u);
    CHECK(gate.offset_high == 0xffff8000u);
    CHECK(gate.type_attributes == 0x8eu && gate.reserved == 0u);
    CHECK(idt_gate_encode(&gate, UINT64_MAX, 0x08u, 0u));
    CHECK(gate.offset_low == 0xffffu && gate.offset_middle == 0xffffu);
    CHECK(gate.offset_high == 0xffffffffu);

    const struct idt_gate before = gate;
    CHECK(!idt_gate_encode(&gate, 0u, 8u, 0u));
    CHECK(memcmp(&gate, &before, sizeof(gate)) == 0);
    CHECK(!idt_gate_encode(&gate, UINT64_C(0x0000800000000000), 8u, 0u));
    CHECK(memcmp(&gate, &before, sizeof(gate)) == 0);
    CHECK(!idt_gate_encode(&gate, UINT64_C(0xffff7fffffffffff), 8u, 0u));
    CHECK(memcmp(&gate, &before, sizeof(gate)) == 0);
    CHECK(!idt_gate_encode(&gate, 1u, 0u, 0u));
    CHECK(!idt_gate_encode(&gate, 1u, 0x0bu, 0u)); /* RPL3 forbidden. */
    CHECK(!idt_gate_encode(&gate, 1u, 0x0cu, 0u)); /* LDT forbidden. */
    CHECK(!idt_gate_encode(&gate, 1u, 8u, 8u));
    CHECK(!idt_gate_encode(NULL, 1u, 8u, 0u));
    CHECK(memcmp(&gate, &before, sizeof(gate)) == 0);
}

static void test_exception_names(void)
{
    CHECK(strcmp(exception_name(0u), "Divide Error") == 0);
    CHECK(strcmp(exception_name(2u), "Non-Maskable Interrupt") == 0);
    CHECK(strcmp(exception_name(6u), "Invalid Opcode") == 0);
    CHECK(strcmp(exception_name(8u), "Double Fault") == 0);
    CHECK(strcmp(exception_name(13u), "General Protection Fault") == 0);
    CHECK(strcmp(exception_name(14u), "Page Fault") == 0);
    CHECK(strcmp(exception_name(15u), "Reserved") == 0);
    CHECK(strcmp(exception_name(21u), "Control Protection") == 0);
    CHECK(strcmp(exception_name(28u), "Hypervisor Injection") == 0);
    CHECK(strcmp(exception_name(29u), "VMM Communication") == 0);
    CHECK(strcmp(exception_name(30u), "Security") == 0);
    CHECK(strcmp(exception_name(31u), "Reserved") == 0);
    CHECK(strcmp(exception_name(32u), "Unexpected Interrupt") == 0);
    CHECK(strcmp(exception_name(255u), "Unexpected Interrupt") == 0);
    CHECK(strcmp(exception_name(UINT64_MAX), "Unexpected Interrupt") == 0);
    for (uint64_t vector = 0u; vector < 32u; ++vector) {
        CHECK(exception_name(vector) != NULL && exception_name(vector)[0] != '\0');
    }
}

static void test_page_fault_bits(void)
{
    const struct page_fault_info zero = exception_decode_page_fault(0u);
    CHECK(!zero.present && !zero.write && !zero.user && !zero.reserved_bit &&
          !zero.instruction_fetch);
    const struct page_fault_info missing_write = exception_decode_page_fault(2u);
    CHECK(!missing_write.present && missing_write.write && !missing_write.user &&
          !missing_write.reserved_bit && !missing_write.instruction_fetch);
    const struct page_fault_info user_protection = exception_decode_page_fault(5u);
    CHECK(user_protection.present && !user_protection.write &&
          user_protection.user && !user_protection.reserved_bit &&
          !user_protection.instruction_fetch);
    const struct page_fault_info bad_table = exception_decode_page_fault(8u);
    CHECK(!bad_table.present && !bad_table.write && !bad_table.user &&
          bad_table.reserved_bit && !bad_table.instruction_fetch);
    const struct page_fault_info execute = exception_decode_page_fault(17u);
    CHECK(execute.present && !execute.write && !execute.user &&
          !execute.reserved_bit && execute.instruction_fetch);
    const struct page_fault_info all = exception_decode_page_fault(UINT64_MAX);
    CHECK(all.present && all.write && all.user && all.reserved_bit &&
          all.instruction_fetch);
    const struct page_fault_info high = exception_decode_page_fault(
        UINT64_C(0xffffffffffffffe0));
    CHECK(!high.present && !high.write && !high.user && !high.reserved_bit &&
          !high.instruction_fetch);
}

struct capture {
    char text[2048];
    size_t used;
};

static void capture_emit(char ch, void *context)
{
    struct capture *capture = context;
    if (capture->used + 1u < sizeof(capture->text)) {
        capture->text[capture->used++] = ch;
        capture->text[capture->used] = '\0';
    }
}

static void test_diagnostic_output(void)
{
    struct capture capture = {0};
    struct interrupt_frame frame = {
        .r15 = 15u, .r14 = 14u, .r13 = 13u, .r12 = 12u,
        .r11 = 11u, .r10 = 10u, .r9 = 9u, .r8 = 8u,
        .rbp = 7u, .rdi = 6u, .rsi = 5u, .rdx = 4u,
        .rcx = 3u, .rbx = 2u, .rax = 1u,
        .vector = 6u, .error_code = 0u,
        .rip = UINT64_C(0xffffffff80001234), .cs = 8u,
        .rflags = 0x202u, .rsp = UINT64_C(0xffffffff800fffe0), .ss = 0x10u
    };
    exception_format(capture_emit, &capture, &frame, UINT64_MAX);
    CHECK(strstr(capture.text, "UTAMO OS KERNEL EXCEPTION") != NULL);
    CHECK(strstr(capture.text, "Exception: Invalid Opcode\nVector:    6\n") != NULL);
    CHECK(strstr(capture.text, "Error:     0x0000000000000000\n") != NULL);
    CHECK(strstr(capture.text, "RIP:       0xffffffff80001234\n") != NULL);
    CHECK(strstr(capture.text, "RSP:       0xffffffff800fffe0\n") != NULL);
    CHECK(strstr(capture.text, "CS:        0x0000000000000008\n") != NULL);
    CHECK(strstr(capture.text, "SS:        0x0000000000000010\n") != NULL);
    CHECK(strstr(capture.text, "RFLAGS:    0x0000000000000202\n") != NULL);
    CHECK(strstr(capture.text, "RAX: 0x0000000000000001 RBX: 0x0000000000000002") != NULL);
    CHECK(strstr(capture.text, "RCX: 0x0000000000000003 RDX: 0x0000000000000004") != NULL);
    CHECK(strstr(capture.text, "RSI: 0x0000000000000005 RDI: 0x0000000000000006") != NULL);
    CHECK(strstr(capture.text, "RBP: 0x0000000000000007 R8:  0x0000000000000008") != NULL);
    CHECK(strstr(capture.text, "R9:  0x0000000000000009 R10: 0x000000000000000a") != NULL);
    CHECK(strstr(capture.text, "R11: 0x000000000000000b R12: 0x000000000000000c") != NULL);
    CHECK(strstr(capture.text, "R13: 0x000000000000000d R14: 0x000000000000000e") != NULL);
    CHECK(strstr(capture.text, "R15: 0x000000000000000f\n") != NULL);
    CHECK(strstr(capture.text, "Fault address:") == NULL);
    CHECK(strstr(capture.text, "System halted.\n") != NULL);

    capture = (struct capture){0};
    frame.vector = 14u;
    frame.error_code = 2u;
    exception_format(capture_emit, &capture, &frame,
                      UINT64_C(0x00007ffffffff000));
    CHECK(strstr(capture.text, "Exception: Page Fault\nVector:    14\n") != NULL);
    CHECK(strstr(capture.text, "Fault address: 0x00007ffffffff000\n") != NULL);
    CHECK(strstr(capture.text, "Present: No\nAccess: Write\nMode: Supervisor\n") != NULL);
    CHECK(strstr(capture.text, "Reserved bit violation: No\nInstruction fetch: No\n") != NULL);

    capture = (struct capture){0};
    frame.error_code = 31u;
    exception_format(capture_emit, &capture, &frame, 0u);
    CHECK(strstr(capture.text, "Present: Yes (protection)\nAccess: Write\nMode: User\n") != NULL);
    CHECK(strstr(capture.text, "Reserved bit violation: Yes\nInstruction fetch: Yes\n") != NULL);
    capture = (struct capture){0};
    exception_format(capture_emit, &capture, NULL, 0u);
    CHECK(capture.used == 0u);
    exception_format(NULL, NULL, &frame, 0u);
}

static void test_memory_diagnostic_output(void)
{
    struct capture capture = {0};
    struct vmm_mapping mapping = {0};
    exception_format_memory(capture_emit, &capture, false, &mapping);
    CHECK(strstr(capture.text, "Virtual memory context") != NULL);
    CHECK(strstr(capture.text, "VMM query: unavailable") != NULL);
    CHECK(strstr(capture.text, "Mapped: No") == NULL);
    capture = (struct capture){0};
    exception_format_memory(capture_emit, &capture, true, &mapping);
    CHECK(strstr(capture.text, "Mapped: No") != NULL);
    CHECK(strstr(capture.text, "Physical:") == NULL);
    CHECK(strstr(capture.text, "Effective flags:") == NULL);
    mapping = (struct vmm_mapping){
        .mapped = true, .physical = UINT64_C(0x1234567),
        .flags = VMM_PRESENT | VMM_WRITABLE | VMM_NX, .page_size = 4096u
    };
    capture = (struct capture){0};
    exception_format_memory(capture_emit, &capture, true, &mapping);
    CHECK(strstr(capture.text, "Mapped: Yes") != NULL);
    CHECK(strstr(capture.text, "Physical: 0x1234567") != NULL);
    CHECK(strstr(capture.text, "Page size: 4096 bytes") != NULL);
    CHECK(strstr(capture.text, "Effective flags: 0x8000000000000003") != NULL);
    CHECK(strstr(capture.text, "Writable: Yes\nUser: No\nNX: Yes") != NULL);
    mapping.flags = VMM_PRESENT | VMM_USER;
    mapping.page_size = UINT64_C(2) * 1024u * 1024u;
    capture = (struct capture){0};
    exception_format_memory(capture_emit, &capture, true, &mapping);
    CHECK(strstr(capture.text, "Page size: 2097152 bytes") != NULL);
    CHECK(strstr(capture.text, "Writable: No\nUser: Yes\nNX: No") != NULL);
    capture = (struct capture){0};
    exception_format_memory(capture_emit, &capture, true, NULL);
    CHECK(strstr(capture.text, "VMM query: unavailable") != NULL);
    exception_format_memory(NULL, NULL, true, &mapping);
}

int main(void)
{
    test_idt_layout();
    test_exception_names();
    test_page_fault_bits();
    test_diagnostic_output();
    test_memory_diagnostic_output();
    (void)printf("UTAMO interrupt tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
