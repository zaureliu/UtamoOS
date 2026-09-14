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

section .text
global io_in32
io_in32:
    mov dx, di
    in eax, dx
    ret
global io_out32
io_out32:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret
global io_out16
io_out16:
    mov dx, di
    mov ax, si
    out dx, ax
    ret
