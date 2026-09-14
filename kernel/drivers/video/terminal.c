/* SPDX-License-Identifier: MIT */
#include <utamo/font.h>
#include <utamo/terminal.h>

bool terminal_init(struct terminal *term, struct framebuffer *fb)
{
    if (term == NULL) {
        return false;
    }
    *term = (struct terminal){0};
    if (fb == NULL || !fb->initialized ||
        fb->width < UTAMO_FONT_CELL_WIDTH ||
        fb->height < UTAMO_FONT_CELL_HEIGHT) {
        return false;
    }
    term->framebuffer = fb;
    term->columns = fb->width / UTAMO_FONT_CELL_WIDTH;
    term->rows = fb->height / UTAMO_FONT_CELL_HEIGHT;
    term->foreground = UINT32_C(0xe6edf3);
    term->background = UINT32_C(0x101820);
    term->initialized = true;
    framebuffer_clear(fb, term->background);
    return true;
}

static void terminal_newline(struct terminal *term)
{
    term->cursor_x = 0u;
    if (term->cursor_y + 1u >= term->rows) {
        framebuffer_clear(term->framebuffer, term->background);
        term->cursor_y = 0u;
    } else {
        ++term->cursor_y;
    }
}

static void terminal_draw_character(struct terminal *term,
                                    unsigned char character)
{
    const size_t origin_x = term->cursor_x * UTAMO_FONT_CELL_WIDTH;
    const size_t origin_y = term->cursor_y * UTAMO_FONT_CELL_HEIGHT;
    for (size_t y = 0u; y < UTAMO_FONT_CELL_HEIGHT; ++y) {
        uint8_t glyph = 0u;
        if (y >= 1u && y < 1u + UTAMO_FONT_HEIGHT * 2u) {
            glyph = font_glyph_row(character, (y - 1u) / 2u);
        }
        for (size_t x = 0u; x < UTAMO_FONT_CELL_WIDTH; ++x) {
            const bool ink = x >= 1u && x <= UTAMO_FONT_WIDTH &&
                (glyph & (UINT32_C(1) << (UTAMO_FONT_WIDTH - x))) != 0u;
            framebuffer_put_pixel(term->framebuffer, origin_x + x,
                                  origin_y + y,
                                  ink ? term->foreground : term->background);
        }
    }
}

void terminal_putc(struct terminal *term, char character)
{
    if (term == NULL || !term->initialized) {
        return;
    }
    if (character == '\n') {
        terminal_newline(term);
        return;
    }
    if (character == '\r') {
        term->cursor_x = 0u;
        return;
    }
    if (character == '\t') {
        if (term->cursor_x >= term->columns) {
            terminal_newline(term);
        }
        const size_t spaces = 4u - (term->cursor_x % 4u);
        for (size_t i = 0u; i < spaces; ++i) {
            terminal_putc(term, ' ');
        }
        return;
    }
    if (character == '\b') {
        if (term->cursor_x != 0u) {
            --term->cursor_x;
            terminal_draw_character(term, (unsigned char)' ');
        }
        return;
    }

    const unsigned char code = (unsigned char)character;
    if (code < 0x20u || code == 0x7fu) {
        return;
    }
    if (term->cursor_x >= term->columns) {
        terminal_newline(term);
    }
    terminal_draw_character(term, code);
    ++term->cursor_x;
}

void terminal_write(struct terminal *term, const char *text)
{
    if (term == NULL || !term->initialized || text == NULL) {
        return;
    }
    while (*text != '\0') {
        terminal_putc(term, *text);
        ++text;
    }
}

void terminal_sink(char character, void *context)
{
    terminal_putc(context, character);
}
