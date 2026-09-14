; SPDX-License-Identifier: MIT
bits 64
default rel

section .text
global _start
extern kernel_main
extern cpu_halt

_start:
    cli
    cld
    ; Own zero-filled stack, 16-byte aligned before CALL (SysV AMD64).
    lea rsp, [rel bootstrap_stack_top]
    and rsp, -16
    xor ebp, ebp
    call kernel_main
    jmp cpu_halt                  ; Defensive even if C ever returns.

section .bss
align 16
bootstrap_stack_bottom:
    resb 65536
bootstrap_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
