/* SPDX-License-Identifier: MIT */
/* Host port/IF model: validates observable 8259 protocol, no privileged IO. */
#include <utamo/cpu.h>
#include <utamo/io.h>
#include <utamo/pic.h>
#include <stdio.h>
#include <stddef.h>
static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
struct write { uint16_t port; uint8_t value; };
static struct write writes[128];
static size_t count;
static uint8_t master_isr;
static uint8_t slave_isr;
static uint64_t cpu_flags = 0x202;
uint64_t cpu_irq_save(void)
{
    const uint64_t old = cpu_flags;
    cpu_flags &= ~UINT64_C(0x200);
    return old;
}
void cpu_irq_restore(uint64_t flags)
{
    cpu_flags = flags;
}
uint8_t io_in8(uint16_t port)
{
    CHECK(port == 0x20 || port == 0xa0);
    return port == 0x20 ? master_isr : slave_isr;
}
void io_out8(uint16_t port, uint8_t value)
{
    if (port == 0x80) {
        return;
    }
    CHECK((cpu_flags & 0x200u) == 0);
    if (count < sizeof(writes) / sizeof(writes[0])) {
        writes[count++] = (struct write){port, value};
    } else {
        CHECK(false);
    }
}
int main(void)
{
    pic_init();
    static const struct write expected[] = {
        {0x21,0xff},{0xa1,0xff},{0x20,0x11},{0xa0,0x11},
        {0x21,0x20},{0xa1,0x28},{0x21,4},{0xa1,2},
        {0x21,1},{0xa1,1},{0x21,0xff},{0xa1,0xff}
    };
    CHECK(count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < count; ++i) {
        CHECK(writes[i].port == expected[i].port);
        CHECK(writes[i].value == expected[i].value);
    }
    CHECK(cpu_flags == 0x202);
    count = 0;
    pic_unmask(0);
    pic_unmask(1);
    CHECK(writes[0].port == 0x21 && writes[0].value == 0xfe);
    CHECK(writes[1].port == 0x21 && writes[1].value == 0xfc);
    pic_unmask(10);
    CHECK(writes[2].port == 0xa1 && writes[2].value == 0xfb);
    CHECK(writes[3].port == 0x21 && writes[3].value == 0xf8);
    pic_mask(10);
    CHECK(writes[4].port == 0xa1 && writes[4].value == 0xff);
    CHECK(writes[5].port == 0x21 && writes[5].value == 0xfc);
    pic_mask(0);
    CHECK(writes[6].value == 0xfd);
    CHECK(cpu_flags == 0x202);
    count = 0;
    pic_unmask(16);
    pic_mask(255);
    pic_send_eoi(16);
    CHECK(!pic_begin_irq(16) && count == 0);
    cpu_flags = 2; /* IRQ entry has IF=0. */
    CHECK(!pic_begin_irq(7));
    CHECK(count == 1 && writes[0].port == 0x20 && writes[0].value == 0x0b);
    count = 0;
    CHECK(!pic_begin_irq(15));
    CHECK(count == 2 && writes[0].port == 0xa0 && writes[0].value == 0x0b);
    CHECK(writes[1].port == 0x20 && writes[1].value == 0x20);
    count = 0;
    master_isr = 0x80;
    slave_isr = 0x80;
    CHECK(pic_begin_irq(7));
    pic_send_eoi(7);
    CHECK(count == 2 && writes[1].port == 0x20 && writes[1].value == 0x20);
    count = 0;
    CHECK(pic_begin_irq(15));
    pic_send_eoi(15);
    CHECK(count == 3 && writes[1].port == 0xa0 && writes[2].port == 0x20);
    CHECK(writes[1].value == 0x20 && writes[2].value == 0x20);
    count = 0;
    CHECK(pic_begin_irq(0));
    pic_send_eoi(0);
    CHECK(count == 1 && writes[0].port == 0x20 && writes[0].value == 0x20);
    pic_mask(1);
    CHECK(cpu_flags == 2);
    (void)printf("UTAMO PIC host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
