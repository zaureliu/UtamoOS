/* SPDX-License-Identifier: MIT */
#include <utamo/interrupts.h>

const char *exception_name(uint64_t vector)
{
    static const char *const names[32] = {
        "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode",
        "Device Not Available", "Double Fault",
        "Coprocessor Segment Overrun (legacy)", "Invalid TSS",
        "Segment Not Present", "Stack Segment Fault", "General Protection Fault",
        "Page Fault", "Reserved", "x87 Floating Point", "Alignment Check",
        "Machine Check", "SIMD Floating Point", "Virtualization",
        "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
        "Security", "Reserved"
    };
    if (vector < 32u) {
        return names[vector];
    }
    return "Unexpected Interrupt";
}

struct page_fault_info exception_decode_page_fault(uint64_t error_code)
{
    return (struct page_fault_info){
        .present = (error_code & UINT64_C(1)) != 0u,
        .write = (error_code & UINT64_C(2)) != 0u,
        .user = (error_code & UINT64_C(4)) != 0u,
        .reserved_bit = (error_code & UINT64_C(8)) != 0u,
        .instruction_fetch = (error_code & UINT64_C(16)) != 0u
    };
}
