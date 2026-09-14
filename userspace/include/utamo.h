/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_USER_RUNTIME_H
#define UTAMO_USER_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include <utamo/syscall_abi.h>
int64_t utamo_call(uint64_t number, uint64_t a, uint64_t b, uint64_t c);
size_t user_strlen(const char *text);
int user_puts(const char *text);
void user_number(uint64_t value);
int64_t user_spawn(const char *path, const char *argument);
int64_t user_wait(uint64_t pid);
int64_t user_open(const char *path);
_Noreturn void user_exit(int64_t status);
int user_main(const char *argument);
#endif
