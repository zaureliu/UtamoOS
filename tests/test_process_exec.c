/* SPDX-License-Identifier: MIT */
/* Actual process registry, ELF publication and WAIT lifecycle. VM, scheduler,
 * CPU and heap boundaries are explicit host models, with injected failures. */
#include <utamo/process.h>
#include <utamo/elf.h>
#include <utamo/heap.h>
#include <utamo/cpu.h>
#include <utamo/scheduler.h>
#include <utamo/syscall_abi.h>
#include <utamo/panic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int checks,failures,live,depth;
static uint64_t irq_flags;
static bool fail_heap,fail_load,fail_publish,fail_copy;
static struct process *published;
static int64_t copied_status;
static const struct vfs_node executable={.path="/program",.type=UTAMO_VFS_FILE,
    .data=(const unsigned char *)"ELF fixture",.size=11u,.mode=0755u};
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x); } } while(0)
uint64_t cpu_irq_save(void) { const uint64_t flags=irq_flags;irq_flags=0u;return flags; }
void cpu_irq_restore(uint64_t flags) { irq_flags=flags; }
void preempt_disable(void) { ++depth; }
void preempt_enable(void) { CHECK(depth!=0u);--depth; }
enum arch_user_status arch_user_init(void) { return UTAMO_USER_READY; }
void *kcalloc(size_t count,size_t size)
{
    if(fail_heap) { return NULL; }
    void *p=calloc(count,size);
    if(p!=NULL) { ++live; }
    return p;
}
bool kfree(void *p) { CHECK(p!=NULL && live!=0u);free(p);--live;return true; }
const struct vfs_node *vfs_lookup(const char *path)
{
    return strcmp(path,"/program")==0 ? &executable : NULL;
}
bool elf_load(struct user_vm *vm,const void *data,size_t size,const char *argument,
              uint64_t *entry,uint64_t *rsp,uint64_t *arg)
{
    (void)data;(void)size;(void)argument;
    if(fail_load) { return false; }
    vm->initialized=true;*entry=0x400000u;*rsp=0x6ffffef0u;*arg=0x6fffff00u;return true;
}
bool user_vm_destroy(struct user_vm *vm)
{
    CHECK(vm->initialized);memset(vm,0,sizeof(*vm));return true;
}
bool user_vm_copy_to(struct user_vm *vm,uint64_t address,const void *source,size_t bytes)
{
    CHECK(vm->initialized && bytes==sizeof(copied_status));
    if(fail_copy || address!=0x10000u) { return false; }
    memcpy(&copied_status,source,bytes);return true;
}
bool scheduler_create_user(const char *name,struct process *owner,uint64_t entry,
                           uint64_t rsp,uint64_t arg,uint64_t *tid)
{
    CHECK(irq_flags==0u && depth==1u && strcmp(name,"/program")==0);
    CHECK(entry==0x400000u && rsp==0x6ffffef0u && arg==0x6fffff00u);
    if(fail_publish) { return false; }
    published=owner;*tid=owner->pid+1u;return true;
}
_Noreturn void kernel_panic(const char *message,const char *file,unsigned int line)
{
    printf("PANIC %s %s %u\n",message,file,line);exit(2);
}
static void finish(struct process *p,int64_t code)
{
    process_mark_exit(p,code,false,0u,0u,0u);
    process_reap(p);
}
int main(void)
{
    CHECK(process_init() && process_available());
    irq_flags=0x200u;
    uint64_t parent_pid=0u,child_pid=0u;
    CHECK(process_spawn_elf("/program","",0u,&parent_pid) && parent_pid==1u);
    struct process *parent=published;
    CHECK(depth==0u && irq_flags==0x200u && live==1u);
    for(unsigned int failure=0u;failure<3u;++failure) {
        fail_heap=failure==0u;fail_load=failure==1u;fail_publish=failure==2u;
        child_pid=UINT64_MAX;
        CHECK(!process_spawn_elf("/program","",parent_pid,&child_pid));
        CHECK(child_pid==UINT64_MAX && depth==0u && live==1u && irq_flags==0x200u);
    }
    fail_heap=false;fail_load=false;fail_publish=false;
    CHECK(process_spawn_elf("/program","",parent_pid,&child_pid) && child_pid==2u);
    struct process *child=published;
    irq_flags=0u; /* The native WAIT boundary runs with IF clear. */
    CHECK(process_collect_child(parent,child_pid,0x10000u)==UTAMO_SYS_EAGAIN);
    struct process unrelated={.pid=999u};
    CHECK(process_collect_child(&unrelated,child_pid,0x10000u)==UTAMO_SYS_ENOENT);
    finish(child,-8); /* Application exit status collides numerically with EAGAIN. */
    fail_copy=true;copied_status=123;
    CHECK(process_collect_child(parent,child_pid,0x10000u)==UTAMO_SYS_EFAULT);
    CHECK(copied_status==123);
    fail_copy=false;
    CHECK(process_collect_child(parent,child_pid,UINT64_MAX)==UTAMO_SYS_EFAULT);
    CHECK(process_collect_child(parent,child_pid,0x10000u)==0 && copied_status==-8);
    CHECK(process_collect_child(parent,child_pid,0x10000u)==UTAMO_SYS_ENOENT);
    CHECK(process_collect_child(NULL,child_pid,0x10000u)==UTAMO_SYS_ENOENT);
    struct process *children[UTAMO_PROCESS_LIMIT-1u];
    for(size_t i=0u;i<UTAMO_PROCESS_LIMIT-1u;++i) {
        CHECK(process_spawn_elf("/program","",parent_pid,&child_pid));
        children[i]=published;
    }
    child_pid=UINT64_MAX;
    CHECK(!process_spawn_elf("/program","",parent_pid,&child_pid) && child_pid==UINT64_MAX);
    for(size_t i=0u;i<UTAMO_PROCESS_LIMIT-1u;++i) { finish(children[i],0); }
    finish(parent,0);
    struct process_stats stats;
    CHECK(process_get_stats(&stats) && stats.active==0u && stats.created==17u &&
          stats.exited==17u && stats.reaped==17u && live==0u && depth==0u);
    printf("ELF process lifecycle: %u checks, %u failures\n",checks,failures);
    return failures!=0u;
}
