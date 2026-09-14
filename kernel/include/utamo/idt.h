/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_IDT_H
#define UTAMO_IDT_H

#include <stdbool.h>
#include <stdint.h>

#define UTAMO_IDT_ENTRIES 256u
#define UTAMO_IDT_INTERRUPT_GATE UINT8_C(0x8e)

/* Architectural 16-byte IA-32e gate. No implementation-defined bitfields. */
struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

/*
 * Pure encoder for present DPL0 interrupt gates and four-level paging.
 * Reject NULL, zero/noncanonical address, null/non-GDT kernel selector, IST>7.
 * Rejected input leaves *gate unchanged. No privileged instruction is issued.
 */
bool idt_gate_encode(struct idt_gate *gate, uint64_t address,
                     uint16_t selector, uint8_t ist);

/* Bootstrap only, IF=0; gdt_init() must precede idt_init(). */
bool idt_set_gate(uint16_t vector, uintptr_t handler, uint8_t ist);
bool idt_init(void);

#endif
