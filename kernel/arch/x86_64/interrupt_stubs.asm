; SPDX-License-Identifier: MIT
bits 64
default rel

section .text
global idt_load
global exception_read_cr2
global exception_fault_ud2
global exception_fault_div0
global exception_fault_page
extern interrupt_dispatch
extern cpu_halt

idt_load:
    lidt [rdi]
    ret

exception_read_cr2:
    mov rax, cr2
    ret

exception_fault_ud2:
    ud2
    jmp cpu_halt

exception_fault_div0:
    mov eax, 1
    xor edx, edx
    xor ecx, ecx
    div rcx                         ; Hardware #DE, no C undefined behavior.
    jmp cpu_halt

exception_fault_page:
    mov rax, 0x00007ffffffff000     ; Canonical; unmapped by our Limine boot.
    mov byte [rax], 0               ; Hardware #PF with W/R=1.
    jmp cpu_halt

; Every entry reaches common_entry with vector,error,RIP,CS,RFLAGS,RSP,SS.
; The CPU supplies error codes for #DF,#TS,#NP,#SS,#GP,#PF,#AC,#CP,#VC,#SX.
; Software INT to those vectors is NOT a supported test: INT supplies no error.
%assign vector 0
%rep 256
global interrupt_stub_%+vector
interrupt_stub_%+vector:
%if vector != 8 && vector != 10 && vector != 11 && vector != 12 && vector != 13 && vector != 14 && vector != 17 && vector != 21 && vector != 29 && vector != 30
    push qword 0
%endif
    push qword vector
    jmp common_entry
%assign vector vector+1
%endrep

common_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    cld                             ; SysV requires DF=0; IRET restores old DF.
    mov rdi, rsp                    ; struct interrupt_frame * argument.
    mov rbx, rsp                    ; RBX is callee-saved by the SysV C call.
    and rsp, -16                    ; RSP is 16-byte aligned immediately pre-CALL.
    call interrupt_dispatch
    mov rsp, rbx                    ; Discard only temporary ABI alignment.

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                     ; Drop vector and normalized error code.
    iretq                           ; Pops full 64-bit RIP,CS,RFLAGS,RSP,SS.

; Read-only signed offsets in this same section need no absolute relocation.
; Keeping the table beside its stubs also avoids NASM 3.01 cross-section warnings.
align 4
global interrupt_stub_table
interrupt_stub_table:
%assign vector 0
%rep 256
    dd interrupt_stub_%+vector - interrupt_stub_table
%assign vector vector+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
