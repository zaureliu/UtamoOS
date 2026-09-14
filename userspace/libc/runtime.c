/* SPDX-License-Identifier: MIT */
#include <utamo.h>
int64_t utamo_call(uint64_t number, uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t result = number;
    __asm__ volatile ("int $0x80" : "+a"(result) : "D"(a), "S"(b), "d"(c) : "memory", "cc");
    return (int64_t)result;
}
size_t user_strlen(const char *text)
{
    size_t length=0u;
    while(text[length]!='\0') { ++length; }
    return length;
}
int user_puts(const char *text)
{
    const size_t length=user_strlen(text);
    return utamo_call(UTAMO_SYS_WRITE,1u,(uint64_t)(uintptr_t)text,length)==(int64_t)length ? 0 : -1;
}
void user_number(uint64_t value)
{
    char buffer[21];
    size_t end=sizeof(buffer)-1u;
    buffer[end]='\0';
    do {
        buffer[--end]=(char)('0'+value%10u);
        value/=10u;
    } while(value!=0u);
    (void)user_puts(buffer+end);
}
int64_t user_spawn(const char *path, const char *argument)
{
    return utamo_call(UTAMO_SYS_SPAWN,(uint64_t)(uintptr_t)path,
                       user_strlen(path),(uint64_t)(uintptr_t)argument);
}
int64_t user_wait(uint64_t pid)
{
    int64_t result, status = 0;
    do {
        result=utamo_call(UTAMO_SYS_WAIT,pid,(uint64_t)(uintptr_t)&status,0u);
        if(result==UTAMO_SYS_EAGAIN) {
            (void)utamo_call(UTAMO_SYS_SLEEP,10u,0u,0u);
        }
    } while(result==UTAMO_SYS_EAGAIN);
    return result == 0 ? status : result;
}
int64_t user_open(const char *path)
{
    return utamo_call(UTAMO_SYS_OPEN,(uint64_t)(uintptr_t)path,user_strlen(path),0u);
}
_Noreturn void user_exit(int64_t status)
{
    (void)utamo_call(UTAMO_SYS_EXIT,(uint64_t)status,0u,0u);
    __asm__ volatile ("ud2");
    for (;;) {}
}
