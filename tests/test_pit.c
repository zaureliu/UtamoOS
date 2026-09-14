/* SPDX-License-Identifier: MIT */
#include <utamo/cpu.h>
#include <utamo/io.h>
#include <utamo/pit.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
static unsigned int checks;
static unsigned int failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
static uint64_t cpu_flags = 2;
static uint16_t ports[3];
static uint8_t values[3];
static size_t count;
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
void io_out8(uint16_t port, uint8_t value)
{
    CHECK(count < 3);
    if (count < 3) {
        ports[count] = port;
        values[count++] = value;
    }
}
int main(void)
{
    pit_init();
    CHECK(count == 3);
    CHECK(ports[0] == 0x43 && values[0] == 0x34);
    CHECK(ports[1] == 0x40 && values[1] == 0x9c);
    CHECK(ports[2] == 0x40 && values[2] == 0x2e);
    CHECK(pit_get_ticks() == 0);
    CHECK(cpu_flags == 2);
    for (unsigned int i = 0; i < 1234; ++i) {
        pit_on_irq();
    }
    cpu_flags = 0x202;
    CHECK(pit_get_ticks() == 1234 && cpu_flags == 0x202);
    CHECK(pit_ticks_to_seconds(0) == 0);
    CHECK(pit_ticks_to_seconds(99) == 0);
    CHECK(pit_ticks_to_seconds(100) == 1);
    CHECK(pit_ticks_to_seconds(1234) == 12);
    CHECK(pit_ticks_to_seconds(UINT64_MAX) == UINT64_MAX / 100);
    CHECK(pit_ticks_to_milliseconds(0) == 0);
    CHECK(pit_ticks_to_milliseconds(1234) == 12340);
    CHECK(pit_ticks_to_milliseconds(UINT64_MAX / 10) == UINT64_MAX - 5);
    CHECK(pit_ticks_to_milliseconds(UINT64_MAX / 10 + 1) == UINT64_MAX);
    CHECK(pit_ticks_to_milliseconds(UINT64_MAX) == UINT64_MAX);
    (void)printf("UTAMO PIT host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
