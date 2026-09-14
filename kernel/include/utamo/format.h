#ifndef UTAMO_FORMAT_H
#define UTAMO_FORMAT_H

/* SPDX-License-Identifier: MIT */
#include <stdarg.h>
#include <stddef.h>

typedef void (*format_emit_fn)(char ch, void *context);

/*
 * Grammar: %s %c %d %u %x %p %% and %lld %llu %llx.
 * %s consumes const char *; cast string literals and char * arguments to
 * const char * explicitly, since ellipsis does not add pointer qualifiers.
 * %d/%u/%x consume int/unsigned int; ll consumes (unsigned) long long.
 * %p consumes void *, prints 0x followed by lowercase hexadecimal digits.
 * No flags, width, precision, floating point, or %n. Unknown sequences are
 * emitted literally without consuming arguments, including trailing '%'.
 * NULL strings and a NULL format produce "(null)". NULL emit is a no-op.
 * All other string pointers must be valid readable NUL-terminated strings.
 * These functions copy va_list; the caller remains responsible for va_end.
 */
void kvformat(format_emit_fn emit, void *context, const char *format,
              va_list args);
void kformat(format_emit_fn emit, void *context, const char *format, ...);

/*
 * Returns the required character count, excluding NUL, saturated at SIZE_MAX.
 * Writes at most capacity - 1 characters, then NUL, if buffer != NULL and
 * capacity > 0. A NULL buffer is a measurement-only call for any capacity.
 * A non-NULL buffer must name a writable object of at least capacity bytes.
 * Source strings/format must not overlap the destination buffer.
 */
size_t kvsnprintf(char *buffer, size_t capacity, const char *format,
                 va_list args);
size_t ksnprintf(char *buffer, size_t capacity, const char *format, ...);

#endif
