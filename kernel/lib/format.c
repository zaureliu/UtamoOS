/* SPDX-License-Identifier: MIT */
#include <utamo/format.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

static void emit_string(format_emit_fn emit, void *context, const char *string)
{
    if (string == NULL) {
        string = "(null)";
    }
    while (*string != '\0') {
        emit(*string, context);
        ++string;
    }
}

static void emit_unsigned(format_emit_fn emit, void *context,
                          unsigned long long value, unsigned int base)
{
    static const char digits[] = "0123456789abcdef";
    /* Enough even for binary; current internal callers use 10 or 16. */
    char reversed[sizeof(unsigned long long) * CHAR_BIT];
    size_t count = 0;

    do {
        reversed[count] = digits[value % base];
        ++count;
        value /= base;
    } while (value != 0);

    while (count != 0) {
        --count;
        emit(reversed[count], context);
    }
}

static void emit_signed(format_emit_fn emit, void *context, long long value)
{
    unsigned long long magnitude = (unsigned long long)value;
    if (value < 0) {
        emit('-', context);
        /* Unsigned subtraction is defined even for LLONG_MIN. */
        magnitude = 0ULL - magnitude;
    }
    emit_unsigned(emit, context, magnitude, 10);
}

void kvformat(format_emit_fn emit, void *context, const char *format,
              va_list args)
{
    if (emit == NULL) {
        return;
    }
    if (format == NULL) {
        emit_string(emit, context, NULL);
        return;
    }

    va_list copy;
    va_copy(copy, args);
    while (*format != '\0') {
        if (*format != '%') {
            emit(*format, context);
            ++format;
            continue;
        }

        const char *sequence = format;
        ++format;
        bool long_long = false;
        if (format[0] == 'l' && format[1] == 'l') {
            long_long = true;
            format += 2;
        }

        const char specifier = *format;
        bool recognized = true;
        if (specifier == 'd') {
            const long long value = long_long ? va_arg(copy, long long) :
                                               va_arg(copy, int);
            emit_signed(emit, context, value);
        } else if (specifier == 'u' || specifier == 'x') {
            const unsigned long long value = long_long ?
                va_arg(copy, unsigned long long) : va_arg(copy, unsigned int);
            emit_unsigned(emit, context, value, specifier == 'u' ? 10U : 16U);
        } else if (!long_long && specifier == 's') {
            emit_string(emit, context, va_arg(copy, const char *));
        } else if (!long_long && specifier == 'c') {
            emit((char)(unsigned char)va_arg(copy, int), context);
        } else if (!long_long && specifier == 'p') {
            const uintptr_t address = (uintptr_t)va_arg(copy, void *);
            emit_string(emit, context, "0x");
            emit_unsigned(emit, context, (unsigned long long)address, 16);
        } else if (!long_long && specifier == '%') {
            emit('%', context);
        } else {
            recognized = false;
        }

        if (!recognized) {
            /* Preserve the prefix, modifier, and unknown specifier. */
            while (sequence != format) {
                emit(*sequence, context);
                ++sequence;
            }
            if (specifier != '\0') {
                emit(specifier, context);
            }
        }
        if (specifier == '\0') {
            break;
        }
        ++format;
    }
    va_end(copy);
}

void kformat(format_emit_fn emit, void *context, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    kvformat(emit, context, format, args);
    va_end(args);
}

struct buffer_output {
    char *buffer;
    size_t capacity;
    size_t length;
};

static void emit_buffer(char ch, void *context)
{
    struct buffer_output *output = context;
    if (output->buffer != NULL && output->capacity != 0 &&
        output->length < output->capacity - 1) {
        output->buffer[output->length] = ch;
    }
    if (output->length != SIZE_MAX) {
        ++output->length;
    }
}

size_t kvsnprintf(char *buffer, size_t capacity, const char *format,
                 va_list args)
{
    struct buffer_output output = {
        .buffer = buffer,
        .capacity = capacity,
        .length = 0
    };
    kvformat(emit_buffer, &output, format, args);

    if (buffer != NULL && capacity != 0) {
        const size_t terminator = output.length < capacity ? output.length :
                                                            capacity - 1;
        buffer[terminator] = '\0';
    }
    return output.length;
}

size_t ksnprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const size_t length = kvsnprintf(buffer, capacity, format, args);
    va_end(args);
    return length;
}
