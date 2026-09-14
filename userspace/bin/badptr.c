/* SPDX-License-Identifier: MIT */
#include <utamo.h>
#define REQUIRE(x) do { if(!(x)) { (void)user_puts("badptr: FAIL\n"); return 1; } } while(0)
int user_main(const char *argument)
{
    (void)argument;
    char data[64];
    const uint64_t bad[]={0u,UINT64_MAX,UINT64_C(0xffffffff80000000),UINT64_C(0x800000000000),UINT64_C(0x400000)};
    const int64_t fd=user_open("/etc/motd");
    REQUIRE(fd==3);
    for(size_t i=0u;i<sizeof(bad)/sizeof(bad[0]);++i) {
        REQUIRE(utamo_call(UTAMO_SYS_READ,3u,bad[i],8u)==UTAMO_SYS_EFAULT);
        REQUIRE(utamo_call(UTAMO_SYS_INFO,bad[i],sizeof(struct utamo_system_info),0u)==UTAMO_SYS_EFAULT);
    }
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,(uint64_t)(uintptr_t)data,1u)==1 && data[0]=='U');
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,0u,1025u)==UTAMO_SYS_EINVAL);
    REQUIRE(utamo_call(UTAMO_SYS_SEEK,3u,UINT64_MAX,0u)==UTAMO_SYS_EINVAL);
    REQUIRE(utamo_call(UTAMO_SYS_OPEN,UINT64_MAX,127u,0u)==UTAMO_SYS_EFAULT);
    REQUIRE(user_open("/etc/../etc/motd")==UTAMO_SYS_EFAULT);
    for(uint64_t next=4u;next<16u;++next) { REQUIRE(user_open("/etc/motd")== (int64_t)next); }
    REQUIRE(user_open("/etc/motd")==UTAMO_SYS_EMFILE);
    REQUIRE(utamo_call(UTAMO_SYS_READ,UINT64_MAX,0u,0u)==UTAMO_SYS_EBADF);
    return user_puts("badptr: PASS (invalid copies, offset rollback, FD capacity)\n");
}
