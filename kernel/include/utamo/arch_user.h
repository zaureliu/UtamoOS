/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_ARCH_USER_H
#define UTAMO_ARCH_USER_H
#include <stdbool.h>
#include <stdint.h>
enum arch_user_status {
    UTAMO_USER_UNINITIALIZED,
    UTAMO_USER_READY,
    UTAMO_USER_NO_NX,
    UTAMO_USER_UNSUPPORTED_CPU,
    UTAMO_USER_UNSUPPORTED_PAGING,
    UTAMO_USER_BAD_CONTEXT,
    UTAMO_USER_SETUP_FAILED
};
/* BSP bootstrap, after GDT/IDT/VMM, IF=0. No user entry before READY.
 * Failed preflight writes no architectural state. A failed hardware readback
 * leaves the syscall gate DPL0. No NX leaves kernel threads operational.
 * No FPU/SIMD, TLS, PCID, LA57, CET or protection-key context is supported. */
enum arch_user_status arch_user_init(void);
enum arch_user_status arch_user_get_status(void);
bool arch_user_ready(void);
/* IF=0, READY. Caller owns a supervisor RW/NX kernel stack and switches CR3.
 * Install RSP0 and reset FS/GS selectors/bases before returning to CPL3.
 * Does not enable interrupts, change CR3 or modify a saved interrupt frame. */
bool arch_user_prepare_return(uint64_t kernel_stack_top);
/* Architecture-only Assembly primitive; no WRMSR until long mode is checked. */
void cpu_user_clear_segments(void);
#endif
