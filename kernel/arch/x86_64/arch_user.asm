; SPDX-License-Identifier: MIT
bits 64
section .text
global cpu_write_cr4
cpu_write_cr4:
    mov cr4, rdi
    ret
global cpu_user_clear_segments
cpu_user_clear_segments:
    xor eax, eax
    mov fs, ax
    mov gs, ax
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
