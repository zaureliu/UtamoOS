/* SPDX-License-Identifier: MIT */
#include <utamo.h>
int user_main(const char *argument)
{
    return user_puts(argument)!=0 || user_puts("\n")!=0;
}
