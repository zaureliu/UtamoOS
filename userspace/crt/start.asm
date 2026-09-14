; SPDX-License-Identifier: MIT
bits 64
section .text.start
global _start
extern user_main
extern user_exit
_start:
    xor ebp, ebp
    cld
    ; Native entry: RSP is 16-byte aligned; RDI points to one argument string.
    call user_main
    movsxd rdi, eax
    call user_exit
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
