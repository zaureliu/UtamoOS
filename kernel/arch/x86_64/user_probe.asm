; SPDX-License-Identifier: MIT
; Controlled CPL3 probes copied to UTAMO_USER_CODE. Entire blob is position
; independent: local RIP-relative references, no kernel label relocations.
bits 64
default rel
%include "build/generated/syscall_abi.inc"
%include "build/generated/user_probe.inc"

section .rodata
align 16
global user_probe_start
global user_probe_end
user_probe_start:
    mov r15, rdi
    cmp edi, UTAMO_PROBE_GOOD
    je .good
    cmp edi, UTAMO_PROBE_KERNEL_READ
    je .kernel_read
    cmp edi, UTAMO_PROBE_KERNEL_WRITE
    je .kernel_write
    cmp edi, UTAMO_PROBE_NX
    je .nx
    cmp edi, UTAMO_PROBE_UD2
    je .ud2
    cmp edi, UTAMO_PROBE_DIV0
    je .div0
    cmp edi, UTAMO_PROBE_CLI
    je .cli
    cmp edi, UTAMO_PROBE_OUT
    je .out
    cmp edi, UTAMO_PROBE_INT240
    je .int240
    cmp edi, UTAMO_PROBE_SYSCALL
    je .syscall
    cmp edi, UTAMO_PROBE_SYSENTER
    je .sysenter
    cmp edi, UTAMO_PROBE_FPU
    je .fpu
    cmp edi, UTAMO_PROBE_BAD_RSP
    je .bad_rsp
    cmp edi, UTAMO_PROBE_POINTERS
    je .pointers
    cmp edi, UTAMO_PROBE_KERNEL_RSP
    je .kernel_rsp
    jmp .unexpected

.good:
    mov r14d, 10
    xor eax, eax
    mov ax, cs
    and eax, 3
    cmp eax, 3
    jne .failure
    inc r14d
    ; Check every byte of the new private data page, as one invariant.
    cld
    mov edi, UTAMO_USER_DATA
    xor eax, eax
    mov ecx, 512
    repe scasq
    jne .failure
    inc r14d
    mov eax, UTAMO_SYS_GETPID
    int 0x80
    test rax, rax
    jle .failure
    mov r12, rax
    mov [abs UTAMO_USER_DATA], rax
    ; No voluntary yield in this part: PIT must be able to preempt user code.
    mov r13d, UTAMO_PROBE_PID_LOOPS
.pid_loop:
    mov eax, UTAMO_SYS_GETPID
    int 0x80
    cmp rax, r12
    jne .failure
    cmp [abs UTAMO_USER_DATA], r12
    jne .failure
    dec r13d
    jnz .pid_loop
    inc r14d
    mov eax, UTAMO_SYS_YIELD
    int 0x80
    test rax, rax
    jne .failure
    inc r14d
    mov edi, 20
    mov eax, UTAMO_SYS_SLEEP
    int 0x80
    test rax, rax
    jne .failure
    inc r14d
    cmp [abs UTAMO_USER_DATA], r12
    jne .failure
    mov edi, 1
    lea rsi, [rel .message]
    mov edx, .message_end - .message
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, .message_end - .message
    jne .failure
    jmp .success

.kernel_read:
    mov rax, 0xffffffff80000000
    mov rax, [rax]
    jmp .unexpected
.kernel_write:
    mov rax, 0xffffffff80000000
    mov byte [rax], 0
    jmp .unexpected
.nx:
    mov eax, UTAMO_USER_DATA
    mov word [rax], 0x0b0f         ; If NX fails, #UD differs from expected #PF.
    call rax
    jmp .unexpected
.ud2:
    ud2
    jmp .unexpected
.div0:
    mov eax, 1
    xor edx, edx
    xor ecx, ecx
    div rcx
    jmp .unexpected
.cli:
    cli
    jmp .unexpected
.out:
    mov dx, 0x80
    xor eax, eax
    out dx, al
    jmp .unexpected
.int240:
    int 240                      ; DPL0 software scheduling entry is forbidden.
    jmp .unexpected
.syscall:
    syscall                      ; EFER.SCE=0 must produce #UD.
    jmp .unexpected
.sysenter:
    sysenter                     ; CS=0 => #GP; some CPUs reject in long mode.
    jmp .unexpected
.fpu:
    fldz                         ; CR0.TS => #NM; no shared FPU state permitted.
    jmp .unexpected
.bad_rsp:
    mov r14, rsp
    mov rsp, 0x0000800000000000
    mov eax, UTAMO_SYS_GETPID
    int 0x80                     ; TSS RSP0 protects entry despite hostile RSP.
    mov rsp, r14
    jmp .unexpected
.kernel_rsp:
    mov r14, rsp
    mov rsp, 0xffffffff80000000
    mov eax, UTAMO_SYS_GETPID
    int 0x80
    mov rsp, r14
    jmp .unexpected

.pointers:
    mov r14d, 30
    mov eax, 0x7fffffff
    int 0x80
    cmp rax, UTAMO_SYS_ENOSYS
    jne .failure
    inc r14d
    mov edi, 9
    xor esi, esi
    mov edx, 1
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EBADF
    jne .failure
    inc r14d
    mov edi, 1
    mov esi, UTAMO_USER_DATA
    mov edx, UTAMO_SYS_WRITE_LIMIT + 1
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EINVAL
    jne .failure
    inc r14d
    mov rsi, 0xffffffff80000000
    mov edx, 8
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EFAULT
    jne .failure
    inc r14d
    mov rsi, 0x0000800000000000
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EFAULT
    jne .failure
    inc r14d
    mov esi, UTAMO_USER_CODE + 0x100000
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EFAULT
    jne .failure
    inc r14d
    mov esi, UTAMO_USER_DATA + 4095
    mov edx, 2
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EFAULT
    jne .failure
    inc r14d
    mov rsi, 0xfffffffffffffffc
    mov edx, 8
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    cmp rax, UTAMO_SYS_EFAULT
    jne .failure
    inc r14d
    xor esi, esi
    xor edx, edx
    mov eax, UTAMO_SYS_WRITE
    int 0x80
    test rax, rax
    jne .failure
.success:
    xor edi, edi
    mov eax, UTAMO_SYS_EXIT
    int 0x80
    ud2
.failure:
    mov edi, r14d
    mov eax, UTAMO_SYS_EXIT
    int 0x80
    ud2
.unexpected:
    lea edi, [r15 + 100]
    mov eax, UTAMO_SYS_EXIT
    int 0x80
    ud2

.message:
    db "Hello from UTAMO ring 3!", 10
.message_end:
user_probe_end:

section .note.GNU-stack noalloc noexec nowrite progbits
