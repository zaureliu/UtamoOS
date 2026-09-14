; SPDX-License-Identifier: MIT
bits 64
section .text
global io_in8
global io_out8

io_in8:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

io_out8:
    mov dx, di
    mov eax, esi
    out dx, al
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
