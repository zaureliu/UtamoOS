/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PANIC_H
#define UTAMO_PANIC_H

_Noreturn void kernel_panic(const char *message, const char *file,
                          unsigned int line);
#define PANIC(message) kernel_panic((message), __FILE__, (unsigned int)__LINE__)

#endif
