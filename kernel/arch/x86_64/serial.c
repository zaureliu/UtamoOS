/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <stdint.h>
#include <utamo/io.h>
#include <utamo/serial.h>

#define UTAMO_COM1 UINT16_C(0x3f8)
#define UTAMO_SERIAL_POLL_LIMIT UINT32_C(100000)

static bool serial_ready;

bool serial_init(void)
{
    serial_ready = false;
    io_out8(UTAMO_COM1 + 1, 0x00); /* UART interrupts disabled. */
    io_out8(UTAMO_COM1 + 3, 0x80); /* DLAB. */
    io_out8(UTAMO_COM1 + 0, 0x01); /* 115200 baud (clock assumption: 1.8432MHz). */
    io_out8(UTAMO_COM1 + 1, 0x00);
    io_out8(UTAMO_COM1 + 3, 0x03); /* 8N1. */
    io_out8(UTAMO_COM1 + 2, 0xc7); /* FIFO enabled, cleared. */
    io_out8(UTAMO_COM1 + 4, 0x1e); /* Internal loopback. */
    io_out8(UTAMO_COM1 + 0, 0xae);
    for (uint32_t attempt = 0; attempt < UTAMO_SERIAL_POLL_LIMIT; ++attempt) {
        const uint8_t status = io_in8(UTAMO_COM1 + 5);
        if (status == 0xff) {
            break;
        }
        if ((status & 0x01u) != 0) {
            serial_ready = io_in8(UTAMO_COM1) == 0xae;
            break;
        }
    }
    io_out8(UTAMO_COM1 + 4, 0x0b); /* Exit loopback; DTR, RTS, OUT2. */
    return serial_ready;
}

bool serial_is_ready(void)
{
    return serial_ready;
}

static bool serial_write_raw(uint8_t value)
{
    if (!serial_ready) {
        return false;
    }
    for (uint32_t attempt = 0; attempt < UTAMO_SERIAL_POLL_LIMIT; ++attempt) {
        const uint8_t status = io_in8(UTAMO_COM1 + 5);
        if (status == 0xff) {
            break;
        }
        if ((status & 0x20u) != 0) {
            io_out8(UTAMO_COM1, value);
            return true;
        }
    }
    serial_ready = false;
    return false;
}

bool serial_write(char ch)
{
    if (ch == '\n' && !serial_write_raw('\r')) {
        return false;
    }
    return serial_write_raw((uint8_t)(unsigned char)ch);
}

bool serial_write_string(const char *text)
{
    if (text == NULL) {
        return false;
    }
    while (*text != '\0') {
        if (!serial_write(*text++)) {
            return false;
        }
    }
    return serial_ready;
}

void serial_sink(char ch, void *context)
{
    (void)context;
    (void)serial_write(ch);
}
