/* SPDX-License-Identifier: MIT */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD FLAGS(6);
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;

    .limine_requests : ALIGN(8)
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests

    . = ALIGN(0x1000);
    __text_start = .;
    .text : {
        KEEP(*(.text.memory_fault_readonly .text.memory_fault_nx))
        *(.text .text.*)
    } :text
    __text_end = ALIGN(0x1000);

    . = ALIGN(0x1000);
    __rodata_start = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    __rodata_end = ALIGN(0x1000);

    . = ALIGN(0x1000);
    __data_start = .;
    .data : { *(.data .data.*) } :data
    __data_end = ALIGN(0x1000);

    /* GNU ld can synthesize these even for a fully static link. Name them
     * explicitly so empty synthetic sections do not become orphan errors.
     * Any actual GOT/PLT or runtime relocation is unsupported and must fail.
     */
    .got : { *(.got .got.* .igot .igot.*) } :data
    .plt : { *(.plt .plt.* .iplt) } :text
    .rela.dyn : { *(.rela .rela.*) } :data
    .rel.dyn : { *(.rel .rel.*) } :data
    ASSERT(SIZEOF(.got) == 0, "GOT is not supported in the static kernel")
    ASSERT(SIZEOF(.plt) == 0, "PLT is not supported in the static kernel")
    ASSERT(SIZEOF(.rela.dyn) == 0, "Runtime RELA relocations are not supported")
    ASSERT(SIZEOF(.rel.dyn) == 0, "Runtime REL relocations are not supported")

    .bss (NOLOAD) : ALIGN(0x1000)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(0x1000);
    __kernel_end = .;

    /* Non-ALLOC DWARF stays in the ELF for GDB, never in a PT_LOAD. */
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) } :NONE
    .debug_abbrev 0 : { *(.debug_abbrev) } :NONE
    .debug_aranges 0 : { *(.debug_aranges) } :NONE
    .debug_line 0 : { *(.debug_line) } :NONE
    .debug_str 0 : { *(.debug_str) } :NONE
    .debug_ranges 0 : { *(.debug_ranges) } :NONE
    .debug_loc 0 : { *(.debug_loc) } :NONE
    .debug_frame 0 : { *(.debug_frame) } :NONE
    .debug_pubnames 0 : { *(.debug_pubnames) } :NONE
    .debug_pubtypes 0 : { *(.debug_pubtypes) } :NONE
    .debug_gnu_pubnames 0 : { *(.debug_gnu_pubnames) } :NONE
    .debug_gnu_pubtypes 0 : { *(.debug_gnu_pubtypes) } :NONE

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
