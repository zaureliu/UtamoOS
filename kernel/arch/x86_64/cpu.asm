; SPDX-License-Identifier: MIT
bits 64
section .text
global cpu_disable_interrupts
global cpu_halt

cpu_disable_interrupts:
    cli
    ret

cpu_halt:
    cli
.stopped:
    hlt
    jmp .stopped                 ; NMI/SMI wakeups must never resume C.

section .note.GNU-stack noalloc noexec nowrite progbits
