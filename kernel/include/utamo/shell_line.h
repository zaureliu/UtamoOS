/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SHELL_LINE_H
#define UTAMO_SHELL_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTAMO_SHELL_LINE_CAPACITY 128u

struct shell_line {
    char bytes[UTAMO_SHELL_LINE_CAPACITY];
    size_t length;
    size_t limit;
};

enum shell_edit {
    UTAMO_SHELL_IGNORED,
    UTAMO_SHELL_APPENDED,
    UTAMO_SHELL_ERASED,
    UTAMO_SHELL_SUBMITTED,
    UTAMO_SHELL_FULL
};

/* limit is clamped to capacity-1; every edit retains a NUL-terminated string. */
void shell_line_init(struct shell_line *line, size_t limit);
enum shell_edit shell_line_feed(struct shell_line *line, char character);

struct shell_command {
    char *name;
    char *arguments;
};

/* In-place ASCII space/tab parsing. No quotes, escapes or heap. Empty=false. */
bool shell_parse_line(char *line, struct shell_command *command);
/* In-place tokenizer for argument validation. NULL at end or invalid input. */
char *shell_next_token(char **cursor);

/*
 * Strict unsigned hexadecimal token: optional 0x/0X and 1..16 ASCII digits.
 * No sign, whitespace or separators. Invalid input leaves *value unchanged.
 */
bool shell_parse_u64_hex(const char *token, uint64_t *value);

#endif
