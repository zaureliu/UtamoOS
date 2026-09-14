/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/syscall_abi.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks,failures;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while(0)
static struct process process;
static struct ramfs fs;
static unsigned char user[4096];
static bool bad_copy;
static unsigned int spawned;
bool user_vm_copy_from(const struct user_vm *vm,void *out,uint64_t src,size_t bytes)
{
    (void)vm;
    if(src>sizeof(user) || bytes>sizeof(user)-src) { return false; }
    memcpy(out,user+src,bytes);return true;
}
bool user_vm_copy_to(struct user_vm *vm,uint64_t dst,const void *src,size_t bytes)
{
    (void)vm;
    if(bytes==0u) { return true; }
    if(bad_copy || dst>sizeof(user) || bytes>sizeof(user)-dst) { return false; }
    memcpy(user+dst,src,bytes);return true;
}
bool process_spawn_elf(const char *path,const char *argument,uint64_t parent,uint64_t *pid)
{
    CHECK(strcmp(path,"/file")==0 && strcmp(argument,"arg")==0 && parent==42u);
    ++spawned;*pid=100u;return true;
}
int64_t process_collect_child(struct process *parent,uint64_t pid,uint64_t address)
{
    CHECK(parent==&process && pid==100u && address==512u);
    return UTAMO_SYS_EAGAIN;
}
bool pmm_get_stats(struct pmm_stats *out)
{
    memset(out,0,sizeof(*out));out->free_frames=123u;return true;
}
uint64_t pit_get_ticks(void) { return 456u; }
static int64_t call(uint64_t n,uint64_t a,uint64_t b,uint64_t c)
{
    return process_file_syscall(&process,n,a,b,c);
}
int main(void)
{
    fs.count=2u;fs.ready=true;
    fs.nodes[0]=(struct vfs_node){.path="/",.type=UTAMO_VFS_DIRECTORY};
    fs.nodes[1]=(struct vfs_node){.path="/file",.type=UTAMO_VFS_FILE,
        .data=(const unsigned char *)"abcde",.size=5u,.mode=0755u};
    CHECK(vfs_mount_root(&fs));
    process.pid=42u;
    memcpy(user+16,"/file",6);
    memcpy(user+64,"arg",4);
    CHECK(call(UTAMO_SYS_OPEN,16,5,0)==3);
    CHECK(call(UTAMO_SYS_READ,3,512,2)==2 && user[512]=='a' && process.files[3].offset==2u);
    bad_copy=true;
    CHECK(call(UTAMO_SYS_READ,3,512,2)==UTAMO_SYS_EFAULT && process.files[3].offset==2u);
    bad_copy=false;
    CHECK(call(UTAMO_SYS_READ,3,512,99)==3 && memcmp(user+512,"cde",3)==0);
    CHECK(call(UTAMO_SYS_READ,3,UINT64_MAX,1)==0);
    CHECK(call(UTAMO_SYS_SEEK,3,UINT64_MAX,0)==UTAMO_SYS_EINVAL);
    CHECK(call(UTAMO_SYS_SEEK,3,0,1)==UTAMO_SYS_EINVAL);
    CHECK(call(UTAMO_SYS_SEEK,3,0,0)==0);
    CHECK(call(UTAMO_SYS_FSTAT,3,0,0)==5);
    CHECK(call(UTAMO_SYS_CLOSE,3,0,0)==0);
    CHECK(call(UTAMO_SYS_CLOSE,3,0,0)==UTAMO_SYS_EBADF);
    CHECK(call(UTAMO_SYS_READ,3,512,0)==UTAMO_SYS_EBADF);
    CHECK(call(UTAMO_SYS_OPEN,16,5,1)==UTAMO_SYS_EINVAL);
    CHECK(call(UTAMO_SYS_OPEN,16,6,0)==UTAMO_SYS_EFAULT);
    CHECK(call(UTAMO_SYS_OPEN,16,128,0)==UTAMO_SYS_EFAULT);
    CHECK(call(UTAMO_SYS_OPEN,UINT64_MAX,5,0)==UTAMO_SYS_EFAULT);
    for(uint64_t fd=3;fd<UTAMO_PROCESS_FD_LIMIT;++fd) {
        CHECK(call(UTAMO_SYS_OPEN,16,5,0)==(int64_t)fd);
    }
    CHECK(call(UTAMO_SYS_OPEN,16,5,0)==UTAMO_SYS_EMFILE);
    CHECK(call(UTAMO_SYS_READ,3,512,1025)==UTAMO_SYS_EINVAL);
    CHECK(call(UTAMO_SYS_READ,UINT64_MAX,512,1)==UTAMO_SYS_EBADF);
    CHECK(call(UTAMO_SYS_FSTAT,UINT64_MAX,0,0)==UTAMO_SYS_EBADF);
    CHECK(call(UTAMO_SYS_SEEK,UINT64_MAX,0,0)==UTAMO_SYS_EBADF);
    CHECK(call(UTAMO_SYS_SPAWN,16,5,64)==100);
    CHECK(spawned==1u);
    CHECK(call(UTAMO_SYS_SPAWN,16,5,UINT64_MAX)==UTAMO_SYS_EFAULT);
    memset(user+64,'x',256);
    CHECK(call(UTAMO_SYS_SPAWN,16,5,64)==UTAMO_SYS_EFAULT && spawned==1u);
    CHECK(call(UTAMO_SYS_WAIT,100,512,0)==UTAMO_SYS_EAGAIN);
    CHECK(call(UTAMO_SYS_INFO,512,sizeof(struct utamo_system_info),0)==0);
    struct utamo_system_info info;
    memcpy(&info,user+512,sizeof(info));
    CHECK(info.pid==42u && info.free_pages==123u && info.ticks==456u && info.page_size==4096u);
    CHECK(call(UTAMO_SYS_INFO,512,31,0)==UTAMO_SYS_EINVAL);
    CHECK(call(UTAMO_SYS_INFO,UINT64_MAX,32,0)==UTAMO_SYS_EFAULT);
    CHECK(call(UINT64_MAX,0,0,0)==UTAMO_SYS_ENOSYS);
    printf("File syscall contracts: %u checks, %u failures\n",checks,failures);
    return failures!=0u;
}
