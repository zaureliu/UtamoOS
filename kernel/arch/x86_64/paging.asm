; SPDX-License-Identifier: MIT
bits 64
section .text
global cpu_cpuid
global cpu_read_cr0, cpu_write_cr0, cpu_read_cr3, cpu_write_cr3, cpu_read_cr4
global cpu_read_msr, cpu_write_msr, cpu_invlpg
global memory_write_address, memory_execute_address
cpu_cpuid:
    push rbx
    mov r8, rdx
    mov eax, edi
    mov ecx, esi
    cpuid
    mov [r8], eax
    mov [r8 + 4], ebx
    mov [r8 + 8], ecx
    mov [r8 + 12], edx
    pop rbx
    ret
cpu_read_cr0:
    mov rax, cr0
    ret
cpu_write_cr0:
    mov cr0, rdi
    ret
cpu_read_cr3:
    mov rax, cr3
    ret
cpu_write_cr3:
    mov cr3, rdi
    ret
cpu_read_cr4:
    mov rax, cr4
    ret
cpu_read_msr:
    mov ecx, edi
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
cpu_write_msr:
    mov ecx, edi
    mov eax, esi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret
cpu_invlpg:
    invlpg [rdi]
    ret
memory_write_address:
    mov [rdi], rsi
    ret
memory_execute_address:
    jmp rdi
section .note.GNU-stack noalloc noexec nowrite progbits
