/* SPDX-License-Identifier: MIT */
/* Host hardware model verifies bounded failure paths and IRQ/main ownership. */
#include <utamo/keyboard.h>

#include <utamo/cpu.h>
#include <utamo/input.h>
#include <utamo/io.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;
static uint8_t queued_bytes[512];
static uint8_t queued_status[512];
static size_t queue_head;
static size_t queue_tail;
static uint8_t configuration;
static uint8_t scan_set;
static bool expect_configuration;
static bool expect_scan_parameter;
static bool write_timeout;
static bool response_timeout;
static bool bad_ack;
static bool resend_once;
static bool resend_forever;
static bool irq_enabled;
static unsigned int f5_writes;
static unsigned int irq_saves;
static unsigned int irq_restores;
static unsigned int port_writes;
static unsigned int status_reads;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

static void enqueue(uint8_t byte, uint8_t status)
{
    if (queue_head < sizeof(queued_bytes)) {
        queued_bytes[queue_head] = byte;
        queued_status[queue_head] = status;
        ++queue_head;
    } else {
        CHECK(false);
    }
}

uint8_t io_in8(uint16_t port)
{
    if (port == 0x64u) {
        ++status_reads;
        if (write_timeout) {
            return 0x02u;
        }
        return queue_tail < queue_head ?
               (uint8_t)(0x01u | queued_status[queue_tail]) : 0u;
    }
    if (port == 0x60u && queue_tail < queue_head) {
        const uint8_t result = queued_bytes[queue_tail];
        ++queue_tail;
        return result;
    }
    CHECK(false);
    return 0u;
}

void io_out8(uint16_t port, uint8_t value)
{
    ++port_writes;
    if (port == 0x64u) {
        if (value == 0xadu) {
            configuration |= 0x10u;
        } else if (value == 0xa7u) {
            configuration |= 0x20u;
        } else if (value == 0xaeu) {
            configuration &= 0xefu;
        } else if (value == 0x20u && !response_timeout) {
            enqueue(configuration, 0u);
        } else if (value == 0x60u) {
            expect_configuration = true;
        } else if (value == 0xabu && !response_timeout) {
            enqueue(0u, 0u);
        }
        return;
    }
    CHECK(port == 0x60u);
    if (expect_configuration) {
        configuration = value;
        expect_configuration = false;
        return;
    }
    if (value == 0xf5u) {
        ++f5_writes;
        if (resend_forever || (resend_once && f5_writes == 1u)) {
            enqueue(0xfeu, 0u);
            return;
        }
    }
    if (bad_ack) {
        enqueue(0xfcu, 0u);
        return;
    }
    enqueue(0xfau, 0u);
    if (expect_scan_parameter) {
        if (value == 0u) {
            enqueue(scan_set, 0u);
        } else {
            scan_set = value;
        }
        expect_scan_parameter = false;
    } else if (value == 0xf0u) {
        expect_scan_parameter = true;
    }
}

uint64_t cpu_irq_save(void)
{
    const uint64_t flags = irq_enabled ? UINT64_C(0x202) : UINT64_C(0x2);
    irq_enabled = false;
    ++irq_saves;
    return flags;
}

void cpu_irq_restore(uint64_t flags)
{
    irq_enabled = (flags & UINT64_C(0x200)) != 0u;
    ++irq_restores;
}

static void reset_hardware(void)
{
    queue_head = 0u;
    queue_tail = 0u;
    configuration = 0x47u; /* Translation and both IRQ bits initially enabled. */
    scan_set = 2u;
    expect_configuration = false;
    expect_scan_parameter = false;
    write_timeout = false;
    response_timeout = false;
    bad_ack = false;
    resend_once = false;
    resend_forever = false;
    irq_enabled = false;
    f5_writes = 0u;
    irq_saves = 0u;
    irq_restores = 0u;
    port_writes = 0u;
    status_reads = 0u;
}

static void deliver(uint8_t byte, uint8_t status)
{
    const bool previous_if = irq_enabled;
    irq_enabled = false;
    enqueue(byte, status);
    keyboard_on_irq();
    irq_enabled = previous_if;
}

static void test_initialization(void)
{
    reset_hardware();
    enqueue(0x1eu, 0u); /* Firmware leftovers must be drained. */
    CHECK(keyboard_init());
    CHECK(scan_set == 1u && f5_writes == 1u);
    CHECK((configuration & 0x73u) == 0x21u);
    CHECK(!keyboard_has_pending());
    CHECK(!irq_enabled);
    reset_hardware();
    resend_once = true;
    CHECK(keyboard_init());
    CHECK(f5_writes == 2u && scan_set == 1u);
    reset_hardware();
    resend_forever = true;
    CHECK(!keyboard_init());
    CHECK(f5_writes == 3u);
    reset_hardware();
    bad_ack = true;
    CHECK(!keyboard_init());
    CHECK(f5_writes == 1u);
    reset_hardware();
    write_timeout = true;
    CHECK(!keyboard_init());
    CHECK(port_writes == 0u && status_reads == 100000u);
    reset_hardware();
    response_timeout = true;
    CHECK(!keyboard_init());
    CHECK(status_reads >= 100000u && status_reads < 100020u);
}

static void test_irq_input(void)
{
    reset_hardware();
    CHECK(keyboard_init());
    irq_enabled = true;
    char character = '?';
    CHECK(!keyboard_read_char(&character));
    CHECK(irq_enabled && irq_saves == irq_restores);
    deliver(0x1eu, 0u);
    CHECK(keyboard_has_pending());
    CHECK(irq_enabled);
    CHECK(keyboard_read_char(&character) && character == 'a');
    CHECK(irq_enabled && irq_saves == irq_restores);
    deliver(0x9eu, 0u);
    CHECK(!keyboard_read_char(&character));
    deliver(0x30u, 0x20u); /* AUX data does not become keyboard input. */
    CHECK(!keyboard_has_pending());
    deliver(0x2au, 0u);
    CHECK(!keyboard_read_char(&character)); /* Shift state only. */
    deliver(0x1eu, 0u);
    CHECK(keyboard_read_char(&character) && character == 'A');
    deliver(0xaau, 0xc0u); /* Lost release/parity: reset held modifiers. */
    CHECK(!keyboard_read_char(&character));
    deliver(0x1eu, 0u);
    CHECK(keyboard_read_char(&character) && character == 'a');
    deliver(0x2au, 0u);
    CHECK(!keyboard_read_char(&character));
    for (size_t i = 0u; i < UTAMO_INPUT_RING_CAPACITY; ++i) {
        deliver(0x30u, 0u);
    }
    CHECK(!keyboard_read_char(&character));
    deliver(0x1eu, 0u);
    CHECK(keyboard_read_char(&character) && character == 'a');
    irq_enabled = false;
    deliver(0x30u, 0u);
    CHECK(keyboard_read_char(&character) && character == 'b');
    CHECK(!irq_enabled && irq_saves == irq_restores);
    CHECK(!keyboard_read_char(NULL));
    const unsigned int before = port_writes;
    keyboard_on_irq(); /* Spurious IRQ with empty controller is harmless. */
    CHECK(port_writes == before);
}

int main(void)
{
    test_initialization();
    test_irq_input();
    (void)printf("UTAMO PS/2 host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
