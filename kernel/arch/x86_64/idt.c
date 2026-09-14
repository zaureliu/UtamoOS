/* SPDX-License-Identifier: MIT */
#include <utamo/descriptors.h>
#include <utamo/gdt.h>
#include <utamo/idt.h>

/* Signed offsets from table base, emitted in the same NASM text section. */
extern const int32_t interrupt_stub_table[UTAMO_IDT_ENTRIES];
void idt_load(const struct descriptor_pointer *pointer);

static _Alignas(16) struct idt_gate idt[UTAMO_IDT_ENTRIES];
static struct descriptor_pointer idtr;

bool idt_set_gate(uint16_t vector, uintptr_t handler, uint8_t ist)
{
    if (vector >= UTAMO_IDT_ENTRIES) {
        return false;
    }
    return idt_gate_encode(&idt[vector], (uint64_t)handler,
                           UTAMO_GDT_CODE_SELECTOR, ist);
}

bool idt_init(void)
{
    for (uint16_t vector = 0u; vector < UTAMO_IDT_ENTRIES; ++vector) {
        uint8_t ist = 0u;
        if (vector == 8u) {
            ist = UTAMO_IST_DOUBLE_FAULT;
        } else if (vector == 2u) {
            ist = UTAMO_IST_NMI;
        } else if (vector == 18u) {
            ist = UTAMO_IST_MACHINE_CHECK;
        }
        /* Sign extension followed by unsigned addition reconstructs the
         * canonical address modulo 2^64 without signed overflow or C
         * pointer arithmetic outside a declared object. */
        const uintptr_t handler = (uintptr_t)interrupt_stub_table +
            (uintptr_t)(intptr_t)interrupt_stub_table[vector];
        if (!idt_set_gate(vector, handler, ist)) {
            return false;
        }
    }
    idtr.limit = (uint16_t)(sizeof(idt) - 1u);
    idtr.base = (uint64_t)(uintptr_t)idt;
    idt_load(&idtr);
    return true;
}
