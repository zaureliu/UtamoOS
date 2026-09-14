; SPDX-License-Identifier: MIT
bits 64
section .text
global cpu_disable_interrupts
global cpu_halt
global cpu_enable_interrupts
global cpu_irq_save
global cpu_irq_restore
global cpu_wait_interrupt

cpu_disable_interrupts:
    cli
    ret

cpu_enable_interrupts:
    sti
    ret

cpu_irq_save:
    pushfq
    pop rax
    cli
    ret

cpu_irq_restore:
    test rdi, 0x200
    jz .disabled
    sti
    ret
.disabled:
    cli
    ret

cpu_wait_interrupt:
    sti
    hlt
    ret

cpu_halt:
    cli
.stopped:
    hlt
    jmp .stopped                 ; NMI/SMI wakeups must never resume C.

section .note.GNU-stack noalloc noexec nowrite progbits
