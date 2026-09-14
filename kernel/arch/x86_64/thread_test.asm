; SPDX-License-Identifier: MIT
bits 64
default rel
section .text
global thread_register_probe
; Controlled thread-context stress only. Preserve the caller's SysV registers.
thread_register_probe:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rax, 0x11
    mov rbx, 0x22
    mov rcx, 0x33
    mov rdx, 0x44
    mov rsi, 0x55
    mov rdi, 0x66
    mov rbp, 0x77
    mov r8, 0x88
    mov r9, 0x99
    mov r10, 0xaa
    mov r11, 0xbb
    mov r12, 0xcc
    mov r13, 0xdd
    mov r14, 0xee
    mov r15, 0xff
    std
    stc
    int 240
    ; PUSHFQ changes no flags; save before CMP. Uses only this thread's stack.
    pushfq
    cmp rax, 0x11
    jne .bad
    cmp rbx, 0x22
    jne .bad
    cmp rcx, 0x33
    jne .bad
    cmp rdx, 0x44
    jne .bad
    cmp rsi, 0x55
    jne .bad
    cmp rdi, 0x66
    jne .bad
    cmp rbp, 0x77
    jne .bad
    cmp r8, 0x88
    jne .bad
    cmp r9, 0x99
    jne .bad
    cmp r10, 0xaa
    jne .bad
    cmp r11, 0xbb
    jne .bad
    cmp r12, 0xcc
    jne .bad
    cmp r13, 0xdd
    jne .bad
    cmp r14, 0xee
    jne .bad
    cmp r15, 0xff
    jne .bad
    pop rax
    and eax, 0x401
    cmp eax, 0x401
    jne .failed
    mov eax, 1
    jmp .done
.bad:
    add rsp, 8
.failed:
    xor eax, eax
.done:
    cld
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
