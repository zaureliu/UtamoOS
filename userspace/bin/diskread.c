/* SPDX-License-Identifier: MIT */
#include <utamo.h>
static unsigned char buffer[1024];
#define REQUIRE(x) do { if (!(x)) { (void)user_puts("diskread: FAIL at line "); user_number(__LINE__); (void)user_puts("\n"); return 1; } } while (0)
int user_main(const char *argument)
{
    (void)argument;
    const int64_t fd=user_open("/disk/CHAIN.BIN");
    REQUIRE(fd==3);
    REQUIRE(utamo_call(UTAMO_SYS_FSTAT,3u,0u,0u)==2049);
    size_t total=0u;
    for (size_t round=0u; round<3u; ++round) {
        const int64_t got=utamo_call(UTAMO_SYS_READ,3u,(uint64_t)(uintptr_t)buffer,sizeof(buffer));
        REQUIRE(got==(round<2u?1024:1));
        for (size_t i=0u;i<(size_t)got;++i) {
            REQUIRE(buffer[i]==(unsigned char)((total+i)*17u+3u));
        }
        total+=(size_t)got;
    }
    REQUIRE(total==2049u);
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,UINT64_MAX,1u)==0);
    REQUIRE(utamo_call(UTAMO_SYS_SEEK,3u,2040u,0u)==2040);
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,UINT64_MAX,9u)==UTAMO_SYS_EFAULT);
    REQUIRE(utamo_call(UTAMO_SYS_READ,3u,(uint64_t)(uintptr_t)buffer,9u)==9);
    REQUIRE(buffer[8]==3u);
    REQUIRE(utamo_call(UTAMO_SYS_CLOSE,3u,0u,0u)==0);
    REQUIRE(user_spawn("/disk/FILE.TXT","")==UTAMO_SYS_EEXEC);
    return user_puts("diskread: PASS (fragmented FAT32, EOF, seek, user-copy rollback)\n");
}
