/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_IO_H
#define UTAMO_IO_H

#include <stdint.h>

uint32_t io_in32(uint16_t port);
void io_out32(uint16_t port, uint32_t value);
void io_out16(uint16_t port, uint16_t value);
uint8_t io_in8(uint16_t port);
void io_out8(uint16_t port, uint8_t value);

#endif
