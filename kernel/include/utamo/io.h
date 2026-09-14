/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_IO_H
#define UTAMO_IO_H

#include <stdint.h>

uint8_t io_in8(uint16_t port);
void io_out8(uint16_t port, uint8_t value);

#endif
