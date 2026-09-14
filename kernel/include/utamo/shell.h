/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SHELL_H
#define UTAMO_SHELL_H

#include <utamo/memory_map.h>
#include <utamo/terminal.h>

/*
 * Main-loop only, one CPU. IRQ handlers must never call the shell/logger.
 * map and terminal remain owned by boot and must outlive the shell.
 * A NULL terminal supports serial-only operation; map must be valid.
 */
void shell_init(const struct memory_map *map, struct terminal *terminal);
void shell_process_input(void);

#endif
