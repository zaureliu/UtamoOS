#ifndef UTAMO_STRING_H
#define UTAMO_STRING_H

#include <stddef.h>

/* SPDX-License-Identifier: MIT */

/*
 * Nonzero ranges must refer to valid objects. memcpy permits nonoverlap or
 * exactly identical source/destination (required by GCC freestanding);
 * memmove accepts overlap. Zero-sized ranges are not dereferenced.
 * String functions require readable NUL-terminated strings; strncmp only
 * requires readable bytes up to the first NUL or count, whichever comes first.
 */
void *memcpy(void *destination, const void *source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
size_t strlen(const char *string);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);

#endif
