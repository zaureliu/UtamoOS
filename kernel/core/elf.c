/* SPDX-License-Identifier: MIT */
#include <utamo/elf.h>
#include <utamo/string.h>
static uint64_t little(const unsigned char *p, size_t size)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < size; ++i) {
        value |= (uint64_t)p[i] << (i * 8u);
    }
    return value;
}

bool elf_validate(const void *image, size_t size, struct elf_plan *out)
{
    const unsigned char *data = image;
    if (data == NULL || out == NULL || size < 64u || size > UTAMO_ELF_FILE_LIMIT ||
        memcmp(data, "\177ELF", 4u) != 0 || data[4] != 2u || data[5] != 1u ||
        data[6] != 1u || data[7] != 0u || data[8] != 0u ||
        little(data + 16u, 2u) != 2u || little(data + 18u, 2u) != 62u ||
        little(data + 20u, 4u) != 1u || little(data + 48u, 4u) != 0u ||
        little(data + 52u, 2u) != 64u || little(data + 54u, 2u) != 56u) {
        return false;
    }
    const uint64_t offset = little(data + 32u, 8u);
    const uint64_t count = little(data + 56u, 2u);
    if (count == 0u || count > UTAMO_ELF_SEGMENT_LIMIT || offset < 64u ||
        offset > size || count > ((uint64_t)size - offset) / 56u) {
        return false;
    }
    struct elf_plan plan = {.entry = little(data + 24u, 8u)};
    bool executable_entry = false;
    for (uint64_t i = 0u; i < count; ++i) {
        const unsigned char *p = data + (size_t)(offset + i * 56u);
        const uint64_t type = little(p, 4u), flags = little(p + 4u, 4u);
        if (type == 0u || type == 4u || type == 6u || type == UINT64_C(0x6474e551)) {
            if (type == UINT64_C(0x6474e551) && (flags & 1u) != 0u) {
                return false;
            }
            continue;
        }
        if (type != 1u) {
            return false;
        }
        struct elf_segment s = {
            .offset = little(p + 8u, 8u), .address = little(p + 16u, 8u),
            .file_size = little(p + 32u, 8u), .memory_size = little(p + 40u, 8u)
        };
        const uint64_t alignment = little(p + 48u, 8u);
        if (s.memory_size == 0u || s.file_size > s.memory_size ||
            s.offset > size || s.file_size > size - s.offset ||
            s.address < USER_VM_MIN || s.address >= USER_VM_END ||
            s.memory_size > USER_VM_END - s.address ||
            (flags & ~UINT64_C(7)) != 0u || (flags & 4u) == 0u ||
            (flags & 3u) == 3u ||
            (s.address & 4095u) != (s.offset & 4095u) ||
            (alignment > 1u && ((alignment & (alignment - 1u)) != 0u ||
             (s.address & (alignment - 1u)) != (s.offset & (alignment - 1u))))) {
            return false;
        }
        s.page_start = s.address & ~UINT64_C(4095);
        s.page_end = (s.address + s.memory_size + 4095u) & ~UINT64_C(4095);
        if (s.page_start < UTAMO_EXEC_STACK_TOP &&
            s.page_end > UTAMO_EXEC_STACK_GUARD) {
            return false;
        }
        const uint64_t pages = (s.page_end - s.page_start) / 4096u;
        if (pages > USER_VM_PAGE_LIMIT - UTAMO_EXEC_STACK_PAGES - plan.pages) {
            return false;
        }
        for (size_t j = 0u; j < plan.count; ++j) {
            if (s.page_start < plan.segments[j].page_end &&
                s.page_end > plan.segments[j].page_start) {
                return false;
            }
        }
        s.access = (flags & 2u) != 0u ? USER_VM_WRITE :
                   (flags & 1u) != 0u ? USER_VM_EXEC : 0u;
        if ((flags & 1u) != 0u && plan.entry >= s.address &&
            plan.entry - s.address < s.file_size) {
            executable_entry = true;
        }
        plan.segments[plan.count++] = s;
        plan.pages += (size_t)pages;
    }
    if (!executable_entry || plan.count == 0u) {
        return false;
    }
    *out = plan;
    return true;
}
