/* SPDX-License-Identifier: MIT */
#include <utamo/string.h>

#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    for (size_t index = 0; index < count; ++index) {
        output[index] = input[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    if (count == 0 || destination == source) {
        return destination;
    }

    /*
     * UTAMO and the supported host tests use a flat address space. Comparing
     * uintptr_t values avoids C relational comparison of unrelated pointers.
     * The subtraction is evaluated only after establishing output > input.
     */
    const uintptr_t output_address = (uintptr_t)destination;
    const uintptr_t input_address = (uintptr_t)source;
    if (output_address < input_address ||
        output_address - input_address >= count) {
        for (size_t index = 0; index < count; ++index) {
            output[index] = input[index];
        }
    } else {
        for (size_t index = count; index != 0; --index) {
            output[index - 1] = input[index - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    const unsigned char byte = (unsigned char)value;

    for (size_t index = 0; index < count; ++index) {
        output[index] = byte;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;

    for (size_t index = 0; index < count; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return left_bytes[index] < right_bytes[index] ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }
    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' &&
           (unsigned char)*left == (unsigned char)*right) {
        ++left;
        ++right;
    }

    const unsigned char left_byte = (unsigned char)*left;
    const unsigned char right_byte = (unsigned char)*right;
    return left_byte < right_byte ? -1 : (left_byte > right_byte ? 1 : 0);
}

int strncmp(const char *left, const char *right, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const unsigned char left_byte = (unsigned char)left[index];
        const unsigned char right_byte = (unsigned char)right[index];
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
        if (left_byte == 0) {
            return 0;
        }
    }
    return 0;
}
