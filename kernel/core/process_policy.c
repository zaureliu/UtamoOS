/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/gdt.h>
#include <utamo/memory.h>

bool process_user_return_valid(const struct interrupt_frame *frame)
{
    /* IOPL, NT, VM, VIF and VIP are never part of the native userspace ABI.
     * IF must stay enabled at CPL3. TF/DF/AC remain architectural user state. */
    const uint64_t forbidden = (UINT64_C(3) << 12u) |
        (UINT64_C(1) << 14u) | (UINT64_C(1) << 17u) |
        (UINT64_C(1) << 19u) | (UINT64_C(1) << 20u);
    return frame != NULL &&
        frame->cs == UTAMO_GDT_USER_CODE_SELECTOR &&
        frame->ss == UTAMO_GDT_USER_DATA_SELECTOR &&
        (frame->rflags & UINT64_C(0x202)) == UINT64_C(0x202) &&
        (frame->rflags & forbidden) == 0u &&
        frame->rip >= USER_VM_MIN && frame->rip < USER_VM_END &&
        frame->rsp >= USER_VM_MIN && frame->rsp < USER_VM_END &&
        memory_is_canonical(frame->rip) && memory_is_canonical(frame->rsp);
}
