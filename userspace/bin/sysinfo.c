/* SPDX-License-Identifier: MIT */
#include <utamo.h>
int user_main(const char *argument)
{
    (void)argument;
    struct utamo_system_info info={0};
    if(utamo_call(UTAMO_SYS_INFO,(uint64_t)(uintptr_t)&info,sizeof(info),0u)!=0) { return 1; }
    (void)user_puts("UTAMO native userspace: x86_64; PID ");
    user_number(info.pid);
    (void)user_puts("; free pages ");
    user_number(info.free_pages);
    (void)user_puts("; PIT ticks ");
    user_number(info.ticks);
    return user_puts("\n");
}
