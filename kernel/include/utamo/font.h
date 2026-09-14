/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_FONT_H
#define UTAMO_FONT_H

#include <stddef.h>
#include <stdint.h>

#define UTAMO_FONT_WIDTH 5u
#define UTAMO_FONT_HEIGHT 7u
#define UTAMO_FONT_CELL_WIDTH 8u
#define UTAMO_FONT_CELL_HEIGHT 16u

/* Bit 4 is the leftmost pixel. Unknown characters use '?'; bad rows are blank. */
uint8_t font_glyph_row(unsigned char character, size_t row);

#endif
