/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_GDT_H
#define UTAMO_GDT_H
#include <utamo/descriptors.h>
#include <stdbool.h>
#define UTAMO_GDT_CODE_SELECTOR UINT16_C(0x08)
#define UTAMO_GDT_DATA_SELECTOR UINT16_C(0x10)
#define UTAMO_GDT_USER_CODE_SELECTOR UINT16_C(0x1b)
#define UTAMO_GDT_USER_DATA_SELECTOR UINT16_C(0x23)
#define UTAMO_GDT_TSS_SELECTOR UINT16_C(0x28)
#define UTAMO_IST_DOUBLE_FAULT 1u
#define UTAMO_IST_NMI 2u
#define UTAMO_IST_MACHINE_CHECK 3u
#define UTAMO_GDT_ENTRIES 7u
#define UTAMO_GDT_CODE UINT64_C(0x00af9a000000ffff)
#define UTAMO_GDT_DATA UINT64_C(0x00cf92000000ffff)
#define UTAMO_GDT_USER_CODE UINT64_C(0x00affa000000ffff)
#define UTAMO_GDT_USER_DATA UINT64_C(0x00cff2000000ffff)
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));
_Static_assert(sizeof(struct tss64) == 104, "64-bit TSS size");
_Static_assert(offsetof(struct tss64, rsp) == 4, "TSS RSP0 offset");
_Static_assert(offsetof(struct tss64, ist) == 36, "TSS IST offset");
_Static_assert(offsetof(struct tss64, iomap_base) == 102, "TSS IO map offset");
/* Bootstrap only, IF=0; live writable GDT owns the CPU-set TSS busy bit. */
void gdt_init(void);
/* IF=0, after init. Canonical higher-half stack top, aligned to 16 bytes.
 * Caller owns/validates supervisor mappings. No LTR/GDT reload per switch. */
bool gdt_set_rsp0(uint64_t stack_top);
void gdt_load(const struct descriptor_pointer *pointer);
void gdt_build(uint64_t entries[UTAMO_GDT_ENTRIES], uint64_t tss_address);
#endif
