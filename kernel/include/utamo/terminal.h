/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_TERMINAL_H
#define UTAMO_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <utamo/framebuffer.h>

struct terminal {
    struct framebuffer *framebuffer;
    size_t columns;
    size_t rows;
    size_t cursor_x;
    size_t cursor_y;
    uint32_t foreground;
    uint32_t background;
    bool initialized;
};

/*
 * Single-CPU early-boot terminal; callers serialize access. fb must outlive term.
 * Initialization clears the display. On bottom overflow the display is cleared
 * and output resumes at the top. No heap or framebuffer reads are required.
 * cursor_x == columns means a pending wrap; cursor_y always remains < rows.
 */
bool terminal_init(struct terminal *term, struct framebuffer *fb);
/* Clear pixels and reset the cursor; preserves colors and geometry. */
void terminal_clear(struct terminal *term);
void terminal_putc(struct terminal *term, char character);
void terminal_write(struct terminal *term, const char *text);
void terminal_sink(char character, void *context);

#endif
