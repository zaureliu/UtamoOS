/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include <utamo/log.h>

#define UTAMO_LOG_SINK_LIMIT 4u

struct log_sink {
    format_emit_fn emit;
    void *context;
};

static struct log_sink sinks[UTAMO_LOG_SINK_LIMIT];
static size_t sink_count;

bool log_add_sink(format_emit_fn emit, void *context)
{
    if (emit == NULL || sink_count == UTAMO_LOG_SINK_LIMIT) {
        return false;
    }
    for (size_t i = 0; i < sink_count; ++i) {
        if (sinks[i].emit == emit && sinks[i].context == context) {
            return false;
        }
    }
    sinks[sink_count].emit = emit;
    sinks[sink_count].context = context;
    ++sink_count;
    return true;
}

static void log_emit(char ch, void *context)
{
    (void)context;
    for (size_t i = 0; i < sink_count; ++i) {
        sinks[i].emit(ch, sinks[i].context);
    }
}

void kvprintf(const char *format, va_list args)
{
    kvformat(log_emit, NULL, format, args);
}

void kprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    kvprintf(format, args);
    va_end(args);
}

void log_message(enum log_level level, const char *format, ...)
{
    const char *label;
    switch (level) {
    case UTAMO_LOG_INFO: label = "INFO "; break;
    case UTAMO_LOG_OK: label = "OK   "; break;
    case UTAMO_LOG_WARN: label = "WARN "; break;
    case UTAMO_LOG_ERROR: label = "ERROR"; break;
    case UTAMO_LOG_DEBUG: label = "DEBUG"; break;
    default: label = "?????"; break;
    }
    kprintf("[ %s ] ", label);
    va_list args;
    va_start(args, format);
    kvprintf(format, args);
    va_end(args);
    kprintf("\n");
}
