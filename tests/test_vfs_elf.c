/* SPDX-License-Identifier: MIT */
#include <utamo/vfs.h>
#include <utamo/elf.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
static unsigned char archive[8192], executable[8192];
static struct ramfs fs, bad;
static size_t append(size_t offset, const char *name, unsigned int mode,
                     const void *data, size_t bytes)
{
    char header[111];
    const size_t length = strlen(name) + 1u;
    (void)snprintf(header, sizeof(header),
        "070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
        1u, mode, 0u, 0u, 1u, 0u, (unsigned int)bytes,
        0u, 0u, 0u, 0u, (unsigned int)length, 0u);
    memcpy(archive + offset, header, 110u);
    offset += 110u;
    memcpy(archive + offset, name, length);
    offset = (offset + length + 3u) & ~(size_t)3u;
    if (bytes != 0u) {
        memcpy(archive + offset, data, bytes);
    }
    return (offset + bytes + 3u) & ~(size_t)3u;
}
static void put(unsigned char *p, uint64_t value, size_t bytes)
{
    for (size_t i = 0u; i < bytes; ++i) {
        p[i] = (unsigned char)(value & 255u);
        value >>= 8u;
    }
}
static void make_elf(void)
{
    memset(executable, 0, sizeof(executable));
    memcpy(executable, "\177ELF\2\1\1", 7u);
    put(executable + 16u, 2u, 2u);
    put(executable + 18u, 62u, 2u);
    put(executable + 20u, 1u, 4u);
    put(executable + 24u, 0x400000u, 8u);
    put(executable + 32u, 64u, 8u);
    put(executable + 52u, 64u, 2u);
    put(executable + 54u, 56u, 2u);
    put(executable + 56u, 1u, 2u);
    put(executable + 64u, 1u, 4u);
    put(executable + 68u, 5u, 4u);
    put(executable + 72u, 4096u, 8u);
    put(executable + 80u, 0x400000u, 8u);
    put(executable + 96u, 8u, 8u);
    put(executable + 104u, 4096u, 8u);
    put(executable + 112u, 4096u, 8u);
    executable[4096] = 0xc3u;
}
static void elf_tests(void)
{
    struct elf_plan plan = {0}, sentinel;
    make_elf();
    CHECK(elf_validate(executable, sizeof(executable), &plan));
    CHECK(plan.count == 1u && plan.pages == 1u && plan.entry == 0x400000u &&
          plan.segments[0].access == USER_VM_EXEC);
    memset(&sentinel, 0x5a, sizeof(sentinel));
    bool all_rejected = true;
    for (size_t i = 0u; i < 4104u; ++i) {
        plan = sentinel;
        if (elf_validate(executable, i, &plan) || memcmp(&plan, &sentinel, sizeof(plan)) != 0) {
            all_rejected = false;
        }
    }
    CHECK(all_rejected); /* Every truncation before the required segment end. */
    const struct {size_t offset, bytes; uint64_t value;} mutations[] = {
        {0,1,0}, {4,1,1}, {5,1,2}, {6,1,0}, {7,1,3}, {8,1,1},
        {16,2,3}, {18,2,3}, {20,4,0}, {24,8,0x500000u},
        {32,8,UINT64_MAX}, {32,8,63}, {48,4,1}, {52,2,63},
        {54,2,55}, {56,2,0}, {56,2,17}, {64,4,2}, {64,4,3},
        {68,4,7}, {68,4,1}, {68,4,13}, {72,8,UINT64_MAX},
        {80,8,0}, {80,8,USER_VM_END}, {80,8,0xffffffff80000000u},
        {80,8,0x400001u}, {96,8,4097}, {104,8,0},
        {104,8,UINT64_MAX}, {104,8,113u*4096u}, {112,8,3},
        {80,8,UTAMO_EXEC_STACK_GUARD}
    };
    for (size_t i = 0u; i < sizeof(mutations)/sizeof(mutations[0]); ++i) {
        make_elf();
        put(executable + mutations[i].offset, mutations[i].value, mutations[i].bytes);
        plan = sentinel;
        CHECK(!elf_validate(executable, sizeof(executable), &plan));
        CHECK(memcmp(&plan, &sentinel, sizeof(plan)) == 0);
    }
    make_elf();
    put(executable+56u,2,2);
    memcpy(executable+120u,executable+64u,56u);
    CHECK(!elf_validate(executable,sizeof(executable),&plan)); /* overlapping pages */
    put(executable+120u,0x6474e551u,4);
    put(executable+124u,7,4);
    CHECK(!elf_validate(executable,sizeof(executable),&plan));
    put(executable+124u,6,4);
    CHECK(elf_validate(executable,sizeof(executable),&plan));
    make_elf();
    put(executable+104u,2u*4096u,8);
    CHECK(elf_validate(executable,sizeof(executable),&plan) && plan.pages==2u);
}
static void vfs_tests(void)
{
    const char *invalid[] = {"", "a", "//", "/a/", "/.", "/..", "/a/../b",
        "/a/./b", "/a//b", "/a\\b", "/a\nb"};
    for (size_t i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        CHECK(!vfs_path_valid(invalid[i]));
    }
    char long_path[129];
    memset(long_path,'a',sizeof(long_path));
    long_path[0]='/';long_path[128]='\0';
    CHECK(!vfs_path_valid(long_path));
    CHECK(vfs_path_valid("/") && vfs_path_valid("/bin/hello"));
    size_t offset=append(0u,"bin",0040755u,NULL,0u);
    offset=append(offset,"bin/hello",0100755u,"contents",8u);
    const size_t trailer=offset;
    offset=append(offset,"TRAILER!!!",0u,NULL,0u);
    CHECK(ramfs_import(&fs,archive,offset));
    CHECK(fs.ready && fs.count==3u);
    CHECK(!ramfs_import(&fs,archive,offset));
    CHECK(vfs_mount_root(&fs));
    CHECK(!vfs_mount_root(&fs));
    CHECK(vfs_child("/",0u)==vfs_lookup("/bin"));
    CHECK(vfs_child("/bin",0u)==vfs_lookup("/bin/hello"));
    CHECK(vfs_child("/bin",1u)==NULL);
    CHECK(vfs_lookup("/missing")==NULL);
    struct vfs_file file={0};
    CHECK(!vfs_open("/bin",&file));
    CHECK(vfs_open("/bin/hello",&file));
    CHECK(!vfs_open("/bin/hello",&file));
    char buffer[16]={0};
    size_t bytes=99;
    CHECK(vfs_read(&file,buffer,3u,&bytes) && bytes==3u && memcmp(buffer,"con",3u)==0);
    CHECK(vfs_read(&file,buffer,sizeof(buffer),&bytes) && bytes==5u && memcmp(buffer,"tents",5u)==0);
    CHECK(vfs_read(&file,buffer,1u,&bytes) && bytes==0u);
    CHECK(!vfs_seek(&file,9u) && file.offset==8u);
    CHECK(vfs_seek(&file,0u));
    CHECK(!vfs_read(&file,NULL,1u,&bytes) && file.offset==0u);
    CHECK(vfs_close(&file));
    CHECK(!vfs_close(&file));
    CHECK(!vfs_read(&file,buffer,1u,&bytes));
    bool truncations=true;
    for (size_t i=0u;i<offset;++i) {
        if (ramfs_import(&bad,archive,i) || bad.count!=0u || bad.ready) {
            truncations=false;
        }
        memset(&bad,0,sizeof(bad));
    }
    CHECK(truncations);
    const unsigned char saved=archive[6];
    archive[6]='g';
    CHECK(!ramfs_import(&bad,archive,offset) && bad.count==0u);
    archive[6]=saved;
    archive[offset]=1u;
    CHECK(!ramfs_import(&bad,archive,offset+1u));
    archive[offset]=0u;
    archive[trailer+6u+6u*8u+7u]='1';
    CHECK(!ramfs_import(&bad,archive,offset));
    memset(archive,0,sizeof(archive));
    offset=append(0u,"bin/hello",0100755u,"x",1u);
    offset=append(offset,"TRAILER!!!",0u,NULL,0u);
    CHECK(!ramfs_import(&bad,archive,offset)); /* Missing parent. */
    const char *names[]={"../escape","/absolute","a//b","a/./b","a/../b"};
    for(size_t i=0u;i<sizeof(names)/sizeof(names[0]);++i) {
        memset(archive,0,sizeof(archive));
        offset=append(0u,names[i],0100644u,"x",1u);
        offset=append(offset,"TRAILER!!!",0u,NULL,0u);
        CHECK(!ramfs_import(&bad,archive,offset));
    }
    memset(archive,0,sizeof(archive));
    offset=append(0u,"x",0100644u,"x",1u);
    offset=append(offset,"x",0100644u,"y",1u);
    offset=append(offset,"TRAILER!!!",0u,NULL,0u);
    CHECK(!ramfs_import(&bad,archive,offset));
    /* Deterministic hostile headers; aggregates 4096 parser invocations. */
    uint32_t seed=0x6a535452u;
    bool bounded=true;
    for(size_t i=0u;i<4096u;++i) {
        memset(archive,0,sizeof(archive));
        for(size_t j=0u;j<512u;++j) {
            seed^=seed<<13u;seed^=seed>>17u;seed^=seed<<5u;
            archive[j]=(unsigned char)seed;
        }
        if(ramfs_import(&bad,archive,512u) || bad.ready || bad.count!=0u) {
            bounded=false;
        }
    }
    CHECK(bounded);
}
int main(void)
{
    elf_tests();
    vfs_tests();
    printf("VFS/ELF: %u checks, %u failures\n",checks,failures);
    return failures!=0u;
}
