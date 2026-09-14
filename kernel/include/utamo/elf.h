/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_ELF_H
#define UTAMO_ELF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utamo/user_vm.h>
#define UTAMO_ELF_SEGMENT_LIMIT 16u
#define UTAMO_ELF_FILE_LIMIT (1024u * 1024u)
#define UTAMO_EXEC_STACK_TOP UINT64_C(0x70000000)
#define UTAMO_EXEC_STACK_PAGES 16u
#define UTAMO_EXEC_STACK_GUARD (UTAMO_EXEC_STACK_TOP - (UTAMO_EXEC_STACK_PAGES + 1u) * UINT64_C(4096))
#define UTAMO_EXEC_ARG_MAX 256u
struct elf_segment {
    uint64_t address, offset, file_size, memory_size, page_start, page_end;
    uint32_t access;
};
struct elf_plan {
    struct elf_segment segments[UTAMO_ELF_SEGMENT_LIMIT];
    uint64_t entry;
    size_t count, pages;
};
/* Pure parser; failure preserves out. Immutable input, no unaligned casts.
 * Static little-endian ET_EXEC only, disjoint page ranges, R required, W^X.
 * Image pages and stack must fit the existing private VM ledger. */
bool elf_validate(const void *image, size_t size, struct elf_plan *out);
/* vm starts zeroed and inactive. Creates and loads an owned private VM.
 * On any failure every allocation is rolled back; out parameters unchanged.
 * The argument is a bounded NUL-terminated kernel string passed in RDI. */
bool elf_load(struct user_vm *vm, const void *image, size_t size,
              const char *argument, uint64_t *entry, uint64_t *rsp,
              uint64_t *user_argument);
#endif
