/* SPDX-License-Identifier: MIT */
#include <utamo.h>
int user_main(const char *argument)
{
    (void)argument;
    if(utamo_call(UTAMO_SYS_GETPID,0u,0u,0u)!=1) { return 1; }
    (void)user_puts("init: PID 1 executing native ELF programs\n");
    const char *programs[]={"/bin/hello","/bin/echo","/bin/sysinfo"};
    for(size_t i=0u;i<sizeof(programs)/sizeof(programs[0]);++i) {
        const int64_t pid=user_spawn(programs[i],"echo from an ELF process");
        if(pid<0 || user_wait((uint64_t)pid)!=0) {
            (void)user_puts("init: child failed\n");
            return 1;
        }
    }
    return user_puts("init: controlled startup complete\n");
}
