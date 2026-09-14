/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_LOG_H
#define UTAMO_LOG_H

#include <stdbool.h>
#include <stdarg.h>
#include <utamo/format.h>

enum log_level {
    UTAMO_LOG_INFO,
    UTAMO_LOG_OK,
    UTAMO_LOG_WARN,
    UTAMO_LOG_ERROR,
    UTAMO_LOG_DEBUG
};

/* Register sinks only during bootstrap with IF=0. Afterward main-context
 * output may be interrupted: IRQ drivers NEVER log or touch these sinks.
 * No SMP, recursive callbacks or sink mutation during output. Fatal CPU
 * exceptions use a separate serial-first path and never resume this logger. */
bool log_add_sink(format_emit_fn emit, void *context);
/* Bounded native write path; bytes are never interpreted as a format string. */
void log_write(const char *bytes, size_t length);
void kvprintf(const char *format, va_list args);
void kprintf(const char *format, ...);
void log_message(enum log_level level, const char *format, ...);

#define LOG_INFO(...) log_message(UTAMO_LOG_INFO, __VA_ARGS__)
#define LOG_OK(...) log_message(UTAMO_LOG_OK, __VA_ARGS__)
#define LOG_WARN(...) log_message(UTAMO_LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(UTAMO_LOG_ERROR, __VA_ARGS__)
#define LOG_DEBUG(...) log_message(UTAMO_LOG_DEBUG, __VA_ARGS__)

#endif
