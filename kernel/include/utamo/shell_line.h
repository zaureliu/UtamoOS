/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SHELL_LINE_H
#define UTAMO_SHELL_LINE_H

#include <stdbool.h>
#include <stddef.h>

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

#endif
