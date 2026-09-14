/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_FILESYSTEM_H
#define UTAMO_FILESYSTEM_H
#include <stdbool.h>
bool filesystem_init(void);
bool filesystem_start_init(void);
bool filesystem_run(const char *path, const char *argument);
bool filesystem_selftest(void);
void filesystem_list(const char *path);
void filesystem_cat(const char *path);
#endif
