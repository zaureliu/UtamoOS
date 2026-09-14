/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_DESCRIPTORS_H
#define UTAMO_DESCRIPTORS_H
#include <stddef.h>
#include <stdint.h>
struct descriptor_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
_Static_assert(sizeof(struct descriptor_pointer) == 10, "GDTR/IDTR size");
_Static_assert(offsetof(struct descriptor_pointer, base) == 2, "GDTR/IDTR base");
#endif
