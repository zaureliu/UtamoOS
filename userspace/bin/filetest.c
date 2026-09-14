/* SPDX-License-Identifier: MIT */
#include <utamo.h>
static volatile unsigned char zero_bss[5000];
static volatile uint64_t initialized_data=UINT64_C(0x123456789abcdef0);
#define REQUIRE(x) do { if(!(x)) { (void)user_puts("filetest: FAIL\n"); return 1; } } while(0)
int user_main(const char *argument)
{
    (void)argument;
    for(size_t i=0u;i<sizeof(zero_bss);++i) { REQUIRE(zero_bss[i]==0u); zero_bss[i]=(unsigned char)i; }
    REQUIRE(initialized_data==UINT64_C(0x123456789abcdef0));
    const int64_t fd=user_open("/etc/motd");
    REQUIRE(fd==3);
    char buffer[128]={0};
    const int64_t size=utamo_call(UTAMO_SYS_FSTAT,(uint64_t)fd,0u,0u);
    REQUIRE(size==(int64_t)(sizeof("UTAMO native VFS and initramfs\n")-1u));
    REQUIRE(utamo_call(UTAMO_SYS_READ,(uint64_t)fd,(uint64_t)(uintptr_t)buffer,5u)==5);
    REQUIRE(buffer[0]=='U' && buffer[4]=='O');
    REQUIRE(utamo_call(UTAMO_SYS_SEEK,(uint64_t)fd,0u,0u)==0);
    REQUIRE(utamo_call(UTAMO_SYS_READ,(uint64_t)fd,(uint64_t)(uintptr_t)buffer,128u)==size);
    REQUIRE(utamo_call(UTAMO_SYS_READ,(uint64_t)fd,UINT64_MAX,1u)==0);
    REQUIRE(utamo_call(UTAMO_SYS_CLOSE,(uint64_t)fd,0u,0u)==0);
    REQUIRE(utamo_call(UTAMO_SYS_CLOSE,(uint64_t)fd,0u,0u)==UTAMO_SYS_EBADF);
    REQUIRE(user_open("/etc/empty")==3);
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,0u,128u)==0);
    REQUIRE(user_open("/missing")==UTAMO_SYS_ENOENT);
    REQUIRE(user_spawn("/etc/not-elf","")==UTAMO_SYS_EEXEC);
    REQUIRE(user_spawn("/etc/motd","")==UTAMO_SYS_EEXEC);
    REQUIRE(utamo_call(UTAMO_SYS_WAIT,1u,0u,0u)==UTAMO_SYS_ENOENT);
    const int64_t child=user_spawn("/bin/echo","filetest child");
    REQUIRE(child>0 && user_wait((uint64_t)child)==0);
    REQUIRE(user_wait((uint64_t)child)==UTAMO_SYS_ENOENT);
    /* Deliberately leave descriptor 3 open: process exit owns its cleanup. */
    return user_puts("filetest: PASS (BSS, data, VFS, spawn, wait, cleanup)\n");
}
