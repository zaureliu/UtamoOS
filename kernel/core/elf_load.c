/* SPDX-License-Identifier: MIT */
#include <utamo/elf.h>
#include <utamo/panic.h>
#include <utamo/string.h>

bool elf_load(struct user_vm *vm, const void *image, size_t size,
              const char *argument, uint64_t *entry, uint64_t *rsp,
              uint64_t *user_argument)
{
    struct elf_plan plan;
    if (vm == NULL || vm->initialized || argument == NULL || entry == NULL ||
        rsp == NULL || user_argument == NULL || !elf_validate(image, size, &plan)) {
        return false;
    }
    size_t length = 0u;
    while (length < UTAMO_EXEC_ARG_MAX && argument[length] != '\0') {
        ++length;
    }
    if (length == UTAMO_EXEC_ARG_MAX || !user_vm_create(vm)) {
        return false;
    }
    bool good = true;
    for (size_t i = 0u; good && i < plan.count; ++i) {
        const struct elf_segment *s = &plan.segments[i];
        for (uint64_t va = s->page_start; good && va < s->page_end; va += 4096u) {
            good = user_vm_alloc_page(vm, va, USER_VM_WRITE);
        }
        uint64_t copied = 0u;
        while (good && copied < s->file_size) {
            const uint64_t left = s->file_size - copied;
            const size_t bytes = left < USER_VM_COPY_LIMIT ? (size_t)left : USER_VM_COPY_LIMIT;
            good = user_vm_copy_to(vm, s->address + copied,
                (const unsigned char *)image + (size_t)(s->offset + copied), bytes);
            copied += bytes;
        }
        for (uint64_t va = s->page_start; good && va < s->page_end; va += 4096u) {
            good = user_vm_protect_page(vm, va, s->access);
        }
    }
    for (size_t i = 0u; good && i < UTAMO_EXEC_STACK_PAGES; ++i) {
        good = user_vm_alloc_page(vm, UTAMO_EXEC_STACK_TOP - (uint64_t)(i + 1u) * 4096u,
                                 USER_VM_WRITE);
    }
    const uint64_t arg = UTAMO_EXEC_STACK_TOP - UTAMO_EXEC_ARG_MAX;
    good = good && user_vm_copy_to(vm, arg, argument, length + 1u);
    if (!good) {
        if (!user_vm_destroy(vm)) {
            PANIC("ELF rollback lost private address-space ownership");
        }
        return false;
    }
    *entry = plan.entry;
    *rsp = arg - 16u; /* Assembly _start receives 16-byte aligned RSP. */
    *user_argument = arg;
    return true;
}
