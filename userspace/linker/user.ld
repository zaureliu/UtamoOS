/* SPDX-License-Identifier: MIT */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x400000;
    .text : { *(.text.start) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { BYTE(0); *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    .data : { BYTE(0); *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
