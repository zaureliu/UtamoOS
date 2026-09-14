/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SERIAL_H
#define UTAMO_SERIAL_H

#include <stdbool.h>

bool serial_init(void);
/* Bounded polling. False disables the backend after a hardware timeout. */
bool serial_write(char ch);
bool serial_write_string(const char *text);
bool serial_is_ready(void);
void serial_sink(char ch, void *context);

#endif
