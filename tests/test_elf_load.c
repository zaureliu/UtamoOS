/* SPDX-License-Identifier: MIT */
/* Real ELF parser/loader; a failure-injecting owned-VM model tests rollback.
 * Hardware page tables/copy permissions are separately tested by user_vm/QEMU. */
#include <utamo/elf.h>
#include <utamo/panic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int checks,failures,operations,fail_at,destroys;
static unsigned char image[8192], memory[USER_VM_PAGE_LIMIT][4096];
static uint64_t addresses[USER_VM_PAGE_LIMIT];
static uint32_t permissions[USER_VM_PAGE_LIMIT];
static size_t pages;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while(0)
static bool operation(void) { ++operations; return operations!=fail_at; }
bool user_vm_create(struct user_vm *vm)
{
    if(!operation()) { return false; }
    vm->initialized=true; return true;
}
bool user_vm_alloc_page(struct user_vm *vm,uint64_t address,uint32_t access)
{
    (void)vm;
    if(!operation() || pages==USER_VM_PAGE_LIMIT) { return false; }
    addresses[pages]=address; permissions[pages]=access;
    memset(memory[pages],0,4096); ++pages; return true;
}
bool user_vm_copy_to(struct user_vm *vm,uint64_t address,const void *source,size_t bytes)
{
    (void)vm;
    if(!operation()) { return false; }
    const unsigned char *src=source;
    while(bytes!=0u) {
        size_t i=0u;
        while(i<pages && addresses[i]!=(address&~UINT64_C(4095))) { ++i; }
        if(i==pages || permissions[i]!=USER_VM_WRITE) { return false; }
        const size_t offset=(size_t)(address&4095u);
        const size_t n=bytes<4096u-offset ? bytes : 4096u-offset;
        memcpy(memory[i]+offset,src,n);
        address+=n;src+=n;bytes-=n;
    }
    return true;
}
bool user_vm_protect_page(struct user_vm *vm,uint64_t address,uint32_t access)
{
    (void)vm;
    if(!operation()) { return false; }
    for(size_t i=0u;i<pages;++i) {
        if(addresses[i]==address) { permissions[i]=access; return true; }
    }
    return false;
}
bool user_vm_destroy(struct user_vm *vm)
{
    ++destroys;pages=0u;memset(vm,0,sizeof(*vm));return true;
}
_Noreturn void kernel_panic(const char *message,const char *file,unsigned int line)
{
    printf("PANIC %s %s %u\n",message,file,line);exit(2);
}
static void put(size_t offset,uint64_t value,size_t bytes)
{
    for(size_t i=0u;i<bytes;++i) { image[offset+i]=(unsigned char)value;value>>=8u; }
}
int main(void)
{
    memcpy(image,"\177ELF\2\1\1",7);
    put(16,2,2);put(18,62,2);put(20,1,4);put(24,0x400000u,8);put(32,64,8);
    put(52,64,2);put(54,56,2);put(56,1,2);
    put(64,1,4);put(68,5,4);put(72,4096,8);put(80,0x400000u,8);
    put(96,16,8);put(104,8192,8);put(112,4096,8);
    memset(image+4096,0x5a,16);
    struct user_vm vm={0};
    uint64_t entry=0,rsp=0,arg=0;
    CHECK(elf_load(&vm,image,sizeof(image),"argument",&entry,&rsp,&arg));
    const unsigned int steps=operations;
    CHECK(pages==18u && vm.initialized && entry==0x400000u && (rsp&15u)==0u);
    CHECK(permissions[0]==USER_VM_EXEC && permissions[1]==USER_VM_EXEC);
    CHECK(memcmp(memory[0],image+4096,16)==0);
    bool zeroed=true;
    for(size_t i=16u;i<4096u;++i) { if(memory[0][i]!=0u) { zeroed=false; } }
    for(size_t i=0u;i<4096u;++i) { if(memory[1][i]!=0u) { zeroed=false; } }
    CHECK(zeroed);
    CHECK(strcmp((char *)memory[2]+(arg&4095u),"argument")==0);
    CHECK(user_vm_destroy(&vm));
    for(unsigned int fail=1u;fail<=steps;++fail) {
        operations=0u;fail_at=fail;destroys=0u;
        entry=1u;rsp=2u;arg=3u;
        CHECK(!elf_load(&vm,image,sizeof(image),"argument",&entry,&rsp,&arg));
        CHECK(!vm.initialized && pages==0u && entry==1u && rsp==2u && arg==3u);
        CHECK(destroys==(fail==1u ? 0u : 1u));
    }
    fail_at=0u;operations=0u;
    char long_arg[UTAMO_EXEC_ARG_MAX+1u];
    memset(long_arg,'a',sizeof(long_arg));long_arg[UTAMO_EXEC_ARG_MAX]='\0';
    CHECK(!elf_load(&vm,image,sizeof(image),long_arg,&entry,&rsp,&arg) && operations==0u);
    printf("ELF loader rollback: %u checks, %u failures\n",checks,failures);
    return failures!=0u;
}
