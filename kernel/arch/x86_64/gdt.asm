; SPDX-License-Identifier: MIT
bits 64
default rel
section .text
global gdt_load
gdt_load:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov ax, 0x28
    ltr ax
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
