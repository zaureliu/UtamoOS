/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <utamo/idt.h>
#include <utamo/gdt.h>
#include <utamo/memory.h>

_Static_assert(sizeof(struct idt_gate) == 16u, "IDT gate size");
_Static_assert(offsetof(struct idt_gate, selector) == 2u, "IDT selector offset");
_Static_assert(offsetof(struct idt_gate, ist) == 4u, "IDT IST offset");
_Static_assert(offsetof(struct idt_gate, type_attributes) == 5u, "IDT type offset");
_Static_assert(offsetof(struct idt_gate, offset_middle) == 6u, "IDT middle offset");
_Static_assert(offsetof(struct idt_gate, offset_high) == 8u, "IDT high offset");
_Static_assert(offsetof(struct idt_gate, reserved) == 12u, "IDT reserved offset");

bool idt_gate_encode(struct idt_gate *gate, uint64_t address,
                     uint16_t selector, uint8_t ist)
{
    if (gate == NULL || address == 0u || !memory_is_canonical(address) || selector == 0u ||
        (selector & 7u) != 0u || ist > 7u) {
        return false;
    }
    *gate = (struct idt_gate){
        .offset_low = (uint16_t)(address & UINT64_C(0xffff)),
        .selector = selector,
        .ist = ist,
        .type_attributes = UTAMO_IDT_INTERRUPT_GATE,
        .offset_middle = (uint16_t)((address >> 16u) & UINT64_C(0xffff)),
        .offset_high = (uint32_t)(address >> 32u),
        .reserved = 0u
    };
    return true;
}

bool idt_user_gate_encode(struct idt_gate *gate, uint64_t address)
{
    struct idt_gate encoded;
    if (gate == NULL ||
        !idt_gate_encode(&encoded, address, UTAMO_GDT_CODE_SELECTOR, 0u)) {
        return false;
    }
    encoded.type_attributes |= UINT8_C(0x60);
    *gate = encoded;
    return true;
}
