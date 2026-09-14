/* SPDX-License-Identifier: MIT */
#include <utamo/shell_line.h>

void shell_line_init(struct shell_line *line, size_t limit)
{
    if (line != NULL) {
        *line = (struct shell_line){0};
        line->limit = limit < UTAMO_SHELL_LINE_CAPACITY ? limit :
                      UTAMO_SHELL_LINE_CAPACITY - 1u;
    }
}

enum shell_edit shell_line_feed(struct shell_line *line, char character)
{
    if (line == NULL) {
        return UTAMO_SHELL_IGNORED;
    }
    if (character == '\n' || character == '\r') {
        return UTAMO_SHELL_SUBMITTED;
    }
    if (character == '\b') {
        if (line->length == 0u) {
            return UTAMO_SHELL_IGNORED;
        }
        --line->length;
        line->bytes[line->length] = '\0';
        return UTAMO_SHELL_ERASED;
    }
    if (character == '\t') {
        character = ' ';
    }
    if ((unsigned char)character < 0x20u ||
        (unsigned char)character > 0x7eu) {
        return UTAMO_SHELL_IGNORED;
    }
    if (line->length == line->limit) {
        return UTAMO_SHELL_FULL;
    }
    line->bytes[line->length] = character;
    ++line->length;
    line->bytes[line->length] = '\0';
    return UTAMO_SHELL_APPENDED;
}

static bool is_space(char character)
{
    return character == ' ' || character == '\t';
}

char *shell_next_token(char **cursor)
{
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }
    char *current = *cursor;
    while (is_space(*current)) {
        ++current;
    }
    if (*current == '\0') {
        *cursor = current;
        return NULL;
    }
    char *token = current;
    while (*current != '\0' && !is_space(*current)) {
        ++current;
    }
    if (*current != '\0') {
        *current = '\0';
        ++current;
    }
    *cursor = current;
    return token;
}

bool shell_parse_line(char *line, struct shell_command *command)
{
    if (command == NULL) {
        return false;
    }
    *command = (struct shell_command){0};
    if (line == NULL) {
        return false;
    }
    /* Remove only outer argument whitespace; echo keeps internal whitespace. */
    size_t length = 0u;
    while (line[length] != '\0') {
        ++length;
    }
    while (length != 0u && is_space(line[length - 1u])) {
        --length;
        line[length] = '\0';
    }
    char *cursor = line;
    command->name = shell_next_token(&cursor);
    if (command->name == NULL) {
        return false;
    }
    while (is_space(*cursor)) {
        ++cursor;
    }
    command->arguments = cursor;
    return true;
}

bool shell_parse_u64_hex(const char *token, uint64_t *value)
{
    if (token == NULL || value == NULL) {
        return false;
    }
    if (token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        token += 2;
    }
    uint64_t result = 0u;
    size_t count = 0u;
    while (*token != '\0') {
        unsigned int digit;
        if (*token >= '0' && *token <= '9') {
            digit = (unsigned int)(*token - '0');
        } else if (*token >= 'a' && *token <= 'f') {
            digit = (unsigned int)(*token - 'a') + 10u;
        } else if (*token >= 'A' && *token <= 'F') {
            digit = (unsigned int)(*token - 'A') + 10u;
        } else {
            return false;
        }
        if (count == 16u || result > (UINT64_MAX - digit) / 16u) {
            return false;
        }
        result = result * 16u + digit;
        ++count;
        ++token;
    }
    if (count == 0u) {
        return false;
    }
    *value = result;
    return true;
}


bool shell_parse_u64_dec(const char *token, uint64_t *value)
{
    if (token == NULL || value == NULL || *token == '\0') {
        return false;
    }
    uint64_t result = 0u;
    for (; *token != '\0'; ++token) {
        if (*token < '0' || *token > '9') {
            return false;
        }
        const unsigned int digit = (unsigned int)(*token - '0');
        if (result > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        result = result * 10u + digit;
    }
    *value = result;
    return true;
}
