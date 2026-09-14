/* SPDX-License-Identifier: MIT */
#include <utamo/interrupts.h>

static void emit_hex64(format_emit_fn emit, void *context, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    emit('0', context);
    emit('x', context);
    for (unsigned int remaining = 16u; remaining != 0u; --remaining) {
        const unsigned int shift = (remaining - 1u) * 4u;
        emit(digits[(value >> shift) & UINT64_C(0xf)], context);
    }
}

static void emit_register(format_emit_fn emit, void *context,
                          const char *label, uint64_t value, char suffix)
{
    kformat(emit, context, "%s", label);
    emit_hex64(emit, context, value);
    emit(suffix, context);
}

void exception_format(format_emit_fn emit, void *context,
                      const struct interrupt_frame *frame, uint64_t cr2)
{
    if (emit == NULL || frame == NULL) {
        return;
    }
    kformat(emit, context,
        "\n========================================\n"
        "UTAMO OS KERNEL EXCEPTION\n"
        "========================================\n"
        "Exception: %s\nVector:    %llu\n",
        exception_name(frame->vector), (unsigned long long)frame->vector);
    emit_register(emit, context, "Error:     ", frame->error_code, '\n');
    emit_register(emit, context, "RIP:       ", frame->rip, '\n');
    emit_register(emit, context, "RSP:       ", frame->rsp, '\n');
    emit_register(emit, context, "CS:        ", frame->cs, '\n');
    emit_register(emit, context, "SS:        ", frame->ss, '\n');
    emit_register(emit, context, "RFLAGS:    ", frame->rflags, '\n');

    emit_register(emit, context, "RAX: ", frame->rax, ' ');
    emit_register(emit, context, "RBX: ", frame->rbx, '\n');
    emit_register(emit, context, "RCX: ", frame->rcx, ' ');
    emit_register(emit, context, "RDX: ", frame->rdx, '\n');
    emit_register(emit, context, "RSI: ", frame->rsi, ' ');
    emit_register(emit, context, "RDI: ", frame->rdi, '\n');
    emit_register(emit, context, "RBP: ", frame->rbp, ' ');
    emit_register(emit, context, "R8:  ", frame->r8, '\n');
    emit_register(emit, context, "R9:  ", frame->r9, ' ');
    emit_register(emit, context, "R10: ", frame->r10, '\n');
    emit_register(emit, context, "R11: ", frame->r11, ' ');
    emit_register(emit, context, "R12: ", frame->r12, '\n');
    emit_register(emit, context, "R13: ", frame->r13, ' ');
    emit_register(emit, context, "R14: ", frame->r14, '\n');
    emit_register(emit, context, "R15: ", frame->r15, '\n');

    if (frame->vector == 14u) {
        const struct page_fault_info fault =
            exception_decode_page_fault(frame->error_code);
        emit_register(emit, context, "Fault address: ", cr2, '\n');
        kformat(emit, context,
            "Present: %s\nAccess: %s\nMode: %s\n"
            "Reserved bit violation: %s\nInstruction fetch: %s\n",
            (const char *)(fault.present ? "Yes (protection)" : "No"),
            (const char *)(fault.write ? "Write" : "Read"),
            (const char *)(fault.user ? "User" : "Supervisor"),
            (const char *)(fault.reserved_bit ? "Yes" : "No"),
            (const char *)(fault.instruction_fetch ? "Yes" : "No"));
    }
    kformat(emit, context, "\nSystem halted.\n");
}
