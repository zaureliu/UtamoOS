/* SPDX-License-Identifier: MIT */
#include <utamo/keyboard.h>

#include <stdint.h>
#include <utamo/cpu.h>
#include <utamo/input.h>
#include <utamo/io.h>

#define UTAMO_PS2_DATA 0x60u
#define UTAMO_PS2_STATUS 0x64u
#define UTAMO_PS2_COMMAND 0x64u
#define UTAMO_PS2_POLL_LIMIT 100000u
#define UTAMO_PS2_DRAIN_LIMIT 256u

static struct input_ring raw_input;
static struct input_decoder decoder;
static bool initialized;

static bool wait_to_write(void)
{
    for (unsigned int i = 0u; i < UTAMO_PS2_POLL_LIMIT; ++i) {
        if ((io_in8(UTAMO_PS2_STATUS) & 0x02u) == 0u) {
            return true;
        }
    }
    return false;
}

static bool controller_command(uint8_t command)
{
    if (!wait_to_write()) {
        return false;
    }
    io_out8(UTAMO_PS2_COMMAND, command);
    return true;
}

static bool write_data(uint8_t value)
{
    if (!wait_to_write()) {
        return false;
    }
    io_out8(UTAMO_PS2_DATA, value);
    return true;
}

static bool read_response(uint8_t *response)
{
    for (unsigned int i = 0u; i < UTAMO_PS2_POLL_LIMIT; ++i) {
        const uint8_t status = io_in8(UTAMO_PS2_STATUS);
        if ((status & 0x01u) != 0u) {
            const uint8_t value = io_in8(UTAMO_PS2_DATA);
            if ((status & 0xc0u) != 0u) {
                return false;
            }
            if ((status & 0x20u) == 0u) {
                *response = value;
                return true;
            }
        }
    }
    return false;
}

static bool keyboard_command(uint8_t command)
{
    for (unsigned int attempt = 0u; attempt < 3u; ++attempt) {
        uint8_t response;
        if (!write_data(command) || !read_response(&response)) {
            return false;
        }
        if (response == 0xfau) {
            return true;
        }
        if (response != 0xfeu) {
            return false;
        }
    }
    return false;
}

static bool write_configuration(uint8_t configuration)
{
    return controller_command(0x60u) && write_data(configuration);
}

bool keyboard_init(void)
{
    initialized = false;
    input_ring_init(&raw_input);
    input_decoder_init(&decoder);
    if (!controller_command(0xadu) || !controller_command(0xa7u)) {
        return false;
    }
    unsigned int drained = 0u;
    while ((io_in8(UTAMO_PS2_STATUS) & 0x01u) != 0u) {
        (void)io_in8(UTAMO_PS2_DATA);
        if (++drained == UTAMO_PS2_DRAIN_LIMIT) {
            return false;
        }
    }
    uint8_t configuration;
    if (!controller_command(0x20u) || !read_response(&configuration)) {
        return false;
    }
    /* IRQs off, translation off, second port clock disabled. */
    configuration = (uint8_t)((configuration & 0xbcu) | 0x20u);
    if (!write_configuration(configuration)) {
        return false;
    }
    uint8_t response;
    if (!controller_command(0xabu) || !read_response(&response) ||
        response != 0x00u || !controller_command(0xaeu)) {
        return false;
    }
    /* Explicit raw set 1, independent of whatever the firmware left behind. */
    if (!keyboard_command(0xf5u) || !keyboard_command(0xf0u) ||
        !keyboard_command(0x01u) || !keyboard_command(0xf0u) ||
        !keyboard_command(0x00u) || !read_response(&response) ||
        response != 0x01u || !keyboard_command(0xf4u)) {
        return false;
    }
    configuration = (uint8_t)((configuration & 0xafu) | 0x01u);
    if (!write_configuration(configuration)) {
        return false;
    }
    initialized = true;
    return true;
}

void keyboard_on_irq(void)
{
    const uint8_t status = io_in8(UTAMO_PS2_STATUS);
    if ((status & 0x01u) == 0u) {
        return;
    }
    const uint8_t byte = io_in8(UTAMO_PS2_DATA);
    if (!initialized || (status & 0x20u) != 0u) {
        return;
    }
    if ((status & 0xc0u) != 0u) {
        input_ring_discard(&raw_input);
        return;
    }
    (void)input_ring_push(&raw_input, byte);
}

bool keyboard_read_char(char *character)
{
    if (character == NULL) {
        return false;
    }
    /* A finite budget also bounds processing of unsupported scan sequences. */
    for (size_t i = 0u; i < UTAMO_INPUT_RING_CAPACITY; ++i) {
        uint8_t byte = 0u;
        bool reset = false;
        const uint64_t flags = cpu_irq_save();
        const bool available = input_ring_pop(&raw_input, &byte, &reset);
        cpu_irq_restore(flags);
        if (reset) {
            input_decoder_init(&decoder);
        }
        if (!available) {
            return false;
        }
        if (input_decode_set1(&decoder, byte, character)) {
            return true;
        }
    }
    return false;
}

bool keyboard_has_pending(void)
{
    const uint64_t flags = cpu_irq_save();
    const bool pending = input_ring_has_pending(&raw_input);
    cpu_irq_restore(flags);
    return pending;
}
