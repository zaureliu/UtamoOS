/* SPDX-License-Identifier: MIT */
#include <utamo/gdt.h>
#include <stdio.h>
static unsigned int checks;
static unsigned int failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #value); } } while (0)
int main(void)
{
    uint64_t entries[UTAMO_GDT_ENTRIES];
    const uint64_t addresses[] = {0, UINT64_C(0xffffffff80012340), UINT64_MAX};
    CHECK(sizeof(struct descriptor_pointer) == 10);
    CHECK(sizeof(struct tss64) == 104);
    CHECK(offsetof(struct tss64, rsp) == 4);
    CHECK(offsetof(struct tss64, ist) == 36);
    CHECK(offsetof(struct tss64, iomap_base) == 102);
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        gdt_build(entries, addresses[i]);
        CHECK(entries[0] == 0);
        CHECK(entries[3] == UINT64_C(0x00affa000000ffff));
        CHECK(entries[4] == UINT64_C(0x00cff2000000ffff));
        CHECK(UTAMO_GDT_USER_CODE_SELECTOR == 0x1bu);
        CHECK(UTAMO_GDT_USER_DATA_SELECTOR == 0x23u);
        CHECK(((entries[1] >> 40u) & 255u) == 0x9au);
        CHECK(((entries[1] >> 52u) & 15u) == 0xau);
        CHECK(((entries[2] >> 40u) & 255u) == 0x92u);
        CHECK(((entries[2] >> 52u) & 15u) == 0xcu);
        CHECK((entries[5] & 65535u) == 103u);
        CHECK(((entries[5] >> 40u) & 255u) == 0x89u);
        CHECK(((entries[5] >> 48u) & 255u) == 0);
        const uint64_t restored = ((entries[5] >> 16u) & UINT64_C(0xffffff)) |
            ((entries[5] >> 56u) << 24u) | (entries[6] << 32u);
        CHECK(restored == addresses[i]);
        CHECK((entries[6] >> 32u) == 0);
    }
    (void)printf("UTAMO GDT host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
