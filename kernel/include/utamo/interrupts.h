/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_INTERRUPTS_H
#define UTAMO_INTERRUPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/format.h>

struct terminal;
struct vmm_mapping;

/*
 * interrupt_stubs.asm saves these fifteen GPRs, followed by normalized vector
 * and error_code. Hardware pushes rip/cs/rflags/rsp/ss in 64-bit mode, even
 * without a privilege change (Intel SDM Vol.3A 6.14.2); this is NOT a 32-bit
 * or compatibility-mode frame. Every slot is eight bytes. No swapgs/user mode.
 */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

_Static_assert(sizeof(struct interrupt_frame) == 176u, "interrupt frame size");
_Static_assert(offsetof(struct interrupt_frame, r15) == 0u, "r15 offset");
_Static_assert(offsetof(struct interrupt_frame, r8) == 56u, "r8 offset");
_Static_assert(offsetof(struct interrupt_frame, rbp) == 64u, "rbp offset");
_Static_assert(offsetof(struct interrupt_frame, rdi) == 72u, "rdi offset");
_Static_assert(offsetof(struct interrupt_frame, rsi) == 80u, "rsi offset");
_Static_assert(offsetof(struct interrupt_frame, rdx) == 88u, "rdx offset");
_Static_assert(offsetof(struct interrupt_frame, rcx) == 96u, "rcx offset");
_Static_assert(offsetof(struct interrupt_frame, rbx) == 104u, "rbx offset");
_Static_assert(offsetof(struct interrupt_frame, rax) == 112u, "rax offset");
_Static_assert(offsetof(struct interrupt_frame, vector) == 120u, "vector offset");
_Static_assert(offsetof(struct interrupt_frame, error_code) == 128u, "error offset");
_Static_assert(offsetof(struct interrupt_frame, rip) == 136u, "rip offset");
_Static_assert(offsetof(struct interrupt_frame, cs) == 144u, "cs offset");
_Static_assert(offsetof(struct interrupt_frame, rflags) == 152u, "rflags offset");
_Static_assert(offsetof(struct interrupt_frame, rsp) == 160u, "rsp offset");
_Static_assert(offsetof(struct interrupt_frame, ss) == 168u, "ss offset");

struct page_fault_info {
    bool present;
    bool write;
    bool user;
    bool reserved_bit;
    bool instruction_fetch;
};

const char *exception_name(uint64_t vector);
struct page_fault_info exception_decode_page_fault(uint64_t error_code);

/* Pure diagnostic formatter. NULL frame or emit is a no-op. CR2 is supplied. */
void exception_format(format_emit_fn emit, void *context,
                      const struct interrupt_frame *frame, uint64_t cr2);

/*
 * Pure optional page-fault mapping snapshot formatter. Never walks page tables.
 * available=false distinguishes unsafe/uninitialized queries from unmapped.
 */
void exception_format_memory(format_emit_fn emit, void *context,
                             bool available, const struct vmm_mapping *mapping);

/* Bootstrap registration, IF=0; term must outlive all handlers. NULL allowed. */
void exception_set_terminal(struct terminal *term);
/* Assembly entry point for every interrupt; no logging in IRQ drivers. */
void interrupt_dispatch(struct interrupt_frame *frame);

/* Debug probes only, NEVER called during normal boot. Halt via exception IDT. */
_Noreturn void exception_fault_ud2(void);
_Noreturn void exception_fault_div0(void);
/* Relies on the current Limine bootstrap leaving the last low-half page unmapped. */
_Noreturn void exception_fault_page(void);

#endif
