/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/cpu.h>
#include <utamo/heap.h>
#include <utamo/log.h>
#include <utamo/panic.h>
#include <utamo/scheduler.h>
#include <utamo/string.h>
#include <utamo/user_probe.h>
#include <utamo/memory.h>
#include <utamo/serial.h>
#include <utamo/elf.h>
#include <utamo/syscall_abi.h>
extern const unsigned char user_probe_start[], user_probe_end[];
static struct process *registry[UTAMO_PROCESS_LIMIT];
static struct process_result history[UTAMO_PROCESS_HISTORY];
static size_t history_cursor;
static uint64_t next_pid = 1u;
static struct process_stats statistics;
static bool initialized;

static void increment(uint64_t *value)
{
    if (*value != UINT64_MAX) {
        ++*value;
    }
}

bool process_init(void)
{
    const uint64_t flags = cpu_irq_save();
    if (initialized || (flags & UINT64_C(0x200)) != 0u) {
        cpu_irq_restore(flags);
        return false;
    }
    statistics.status = arch_user_init();
    initialized = true;
    cpu_irq_restore(flags);
    return true;
}

bool process_available(void)
{
    return initialized && statistics.status == UTAMO_USER_READY;
}

bool process_spawn_probe(unsigned int probe, uint64_t *out_pid)
{
    const uint64_t flags = cpu_irq_save();
    if (!process_available() || out_pid == NULL || probe >= UTAMO_PROBE_COUNT ||
        (flags & UINT64_C(0x200)) == 0u || next_pid > INT64_MAX) {
        cpu_irq_restore(flags);
        return false;
    }
    /* Construction is thread-only. IRQs may run; another thread cannot see a
     * partially published process or consume this registry reservation. */
    preempt_disable();
    cpu_irq_restore(flags);
    size_t slot = 0u;
    while (slot < UTAMO_PROCESS_LIMIT && registry[slot] != NULL) {
        ++slot;
    }
    if (slot == UTAMO_PROCESS_LIMIT) {
        preempt_enable();
        return false;
    }
    struct process *const process = kcalloc(1u, sizeof(*process));
    if (process == NULL) {
        preempt_enable();
        return false;
    }
    const size_t image_size = (size_t)((uintptr_t)user_probe_end -
                                      (uintptr_t)user_probe_start);
    bool good = image_size != 0u && image_size <= MEMORY_PAGE_SIZE &&
                user_vm_create(&process->vm) &&
                user_vm_alloc_page(&process->vm, UTAMO_USER_CODE, USER_VM_WRITE) &&
                user_vm_copy_to(&process->vm, UTAMO_USER_CODE,
                                user_probe_start, image_size) &&
                user_vm_protect_page(&process->vm, UTAMO_USER_CODE, USER_VM_EXEC) &&
                user_vm_alloc_page(&process->vm, UTAMO_USER_DATA, USER_VM_WRITE);
    for (size_t i = 0u; good && i < UTAMO_USER_STACK_PAGES; ++i) {
        good = user_vm_alloc_page(&process->vm,
            UTAMO_USER_STACK_TOP - (uint64_t)(i + 1u) * MEMORY_PAGE_SIZE,
            USER_VM_WRITE);
    }
    process->pid = next_pid;
    process->state = UTAMO_PROCESS_READY;
    memcpy(process->name, "user-probe", sizeof("user-probe"));
    const uint64_t publish_flags = cpu_irq_save();
    if (good) {
        good = scheduler_create_user(process->name, process, UTAMO_USER_CODE,
            UTAMO_USER_STACK_TOP - 8u, probe, &process->tid);
    }
    if (good) {
        registry[slot] = process;
        ++next_pid;
        ++statistics.active;
        increment(&statistics.created);
        *out_pid = process->pid;
    } else {
        if ((process->vm.initialized && !user_vm_destroy(&process->vm)) ||
            !kfree(process)) {
            PANIC("Cannot roll back process construction");
        }
    }
    cpu_irq_restore(publish_flags);
    preempt_enable();
    return good;
}

bool process_get_stats(struct process_stats *out)
{
    if (out == NULL) {
        return false;
    }
    const uint64_t flags = cpu_irq_save();
    const bool ready = initialized;
    if (ready) {
        *out = statistics;
    }
    cpu_irq_restore(flags);
    return ready;
}

bool process_get_result(uint64_t pid, struct process_result *out)
{
    if (pid == 0u || out == NULL) {
        return false;
    }
    const uint64_t flags = cpu_irq_save();
    bool found = false;
    for (size_t i = 0u; i < UTAMO_PROCESS_HISTORY; ++i) {
        if (history[i].pid == pid) {
            *out = history[i];
            found = true;
            break;
        }
    }
    cpu_irq_restore(flags);
    return found;
}

bool process_query(uint64_t pid, uint64_t virt, struct vmm_mapping *out)
{
    const uint64_t flags = cpu_irq_save();
    bool valid = false;
    for (size_t i = 0u; i < UTAMO_PROCESS_LIMIT; ++i) {
        if (registry[i] != NULL && registry[i]->pid == pid &&
            registry[i]->state == UTAMO_PROCESS_READY) {
            valid = user_vm_query(&registry[i]->vm, virt, out);
            break;
        }
    }
    cpu_irq_restore(flags);
    return valid;
}

void process_record_syscall(struct process *process)
{
    if (process == NULL || process->state != UTAMO_PROCESS_READY) {
        PANIC("Syscall without a live process");
    }
    increment(&process->syscalls);
    increment(&statistics.syscalls);
}

void process_mark_exit(struct process *process, int64_t code, bool faulted,
                       uint64_t vector, uint64_t error, uint64_t address)
{
    if (process == NULL || process->state != UTAMO_PROCESS_READY) {
        PANIC("Process exited twice");
    }
    process->state = UTAMO_PROCESS_EXITED;
    process->exit_code = code;
    process->faulted = faulted;
    process->fault_vector = vector;
    process->fault_error = error;
    process->fault_address = address;
    increment(&statistics.exited);
    if (faulted) {
        increment(&statistics.user_faults);
    }
}

void process_reap(struct process *process)
{
    size_t slot = 0u;
    while (slot < UTAMO_PROCESS_LIMIT && registry[slot] != process) {
        ++slot;
    }
    if (process == NULL || slot == UTAMO_PROCESS_LIMIT ||
        process->state != UTAMO_PROCESS_EXITED || statistics.active == 0u ||
        !user_vm_destroy(&process->vm)) {
        PANIC("Process reaper lost exclusive address-space ownership");
    }
    history[history_cursor] = (struct process_result){
        .pid = process->pid, .parent_pid = process->parent_pid,
        .exit_code = process->exit_code,
        .faulted = process->faulted, .fault_vector = process->fault_vector,
        .fault_error = process->fault_error,
        .fault_address = process->fault_address, .syscalls = process->syscalls
    };
    history_cursor = (history_cursor + 1u) % UTAMO_PROCESS_HISTORY;
    registry[slot] = NULL;
    --statistics.active;
    increment(&statistics.reaped);
    if (!kfree(process)) {
        PANIC("Cannot release process metadata");
    }
}

struct interrupt_frame *process_on_fault(struct interrupt_frame *frame,
                                        uint64_t vector, uint64_t error,
                                        uint64_t address)
{
    struct process *const process = scheduler_current_process();
    if (process == NULL || (frame->cs & 3u) != 3u) {
        PANIC("User fault without a user process");
    }
    /* This path uses only protected kernel buffers, never the user stack.
     * Complete serial diagnostics first; the ordinary framebuffer logger then
     * receives a short event. Critical IST exceptions never take this path. */
    struct interrupt_frame report = *frame;
    report.vector = vector;
    report.error_code = error;
    exception_format_user(serial_sink, NULL, &report, address, process->pid);
    if (vector == 14u) {
        struct vmm_mapping mapping = {0};
        const bool available = address < USER_VM_END ?
            user_vm_query(&process->vm, address, &mapping) :
            vmm_query_page(address, &mapping);
        if (available && address >= USER_VM_END) {
            mapping.flags &= ~VMM_USER;
        }
        exception_format_memory(serial_sink, NULL, available, &mapping);
    }
    kprintf("User process fault: PID %llu vector %llu error 0x%llx; terminated\n",
            (unsigned long long)process->pid, (unsigned long long)vector,
            (unsigned long long)error);
    process_mark_exit(process, -(int64_t)(128u + vector), true,
                      vector, error, address);
    return scheduler_exit_user(frame);
}

/* ELF contents are immutable mounted files. Every unpublished allocation is
 * exclusively owned here; publication is the same IF=0 scheduler transaction
 * used by the embedded probes. SPAWN creates a child; it does not replace self. */
bool process_spawn_elf(const char *path, const char *argument, uint64_t parent_pid,
                       uint64_t *out_pid)
{
    if (!process_available() || out_pid == NULL || path == NULL || argument == NULL) {
        return false;
    }
    preempt_disable();
    const uint64_t flags = cpu_irq_save();
    size_t slot = 0u;
    while (slot < UTAMO_PROCESS_LIMIT && registry[slot] != NULL) {
        ++slot;
    }
    const struct vfs_node *node = vfs_lookup(path);
    if (slot == UTAMO_PROCESS_LIMIT || next_pid > INT64_MAX || node == NULL ||
        node->type != UTAMO_VFS_FILE || (node->mode & 0111u) == 0u) {
        cpu_irq_restore(flags);
        preempt_enable();
        return false;
    }
    struct process *process = kcalloc(1u, sizeof(*process));
    if (process == NULL) {
        cpu_irq_restore(flags);
        preempt_enable();
        return false;
    }
    uint64_t entry = 0u, rsp = 0u, user_argument = 0u;
    bool good = elf_load(&process->vm, node->data, node->size, argument,
                         &entry, &rsp, &user_argument);
    process->pid = next_pid;
    process->parent_pid = parent_pid;
    process->state = UTAMO_PROCESS_READY;
    size_t length = strlen(path);
    if (length >= sizeof(process->name)) {
        length = sizeof(process->name) - 1u;
    }
    memcpy(process->name, path, length);
    if (good) {
        good = scheduler_create_user(process->name, process, entry, rsp,
                                     user_argument, &process->tid);
    }
    if (good) {
        registry[slot] = process;
        ++next_pid;
        ++statistics.active;
        increment(&statistics.created);
        *out_pid = process->pid;
    } else if ((process->vm.initialized && !user_vm_destroy(&process->vm)) ||
               !kfree(process)) {
        PANIC("Cannot roll back ELF process publication");
    }
    cpu_irq_restore(flags);
    preempt_enable();
    return good;
}

int64_t process_collect_child(struct process *parent, uint64_t child_pid,
                              uint64_t status_address)
{
    const uint64_t parent_pid = parent != NULL ? parent->pid : 0u;
    const uint64_t flags = cpu_irq_save();
    int64_t result = UTAMO_SYS_ENOENT;
    if (parent_pid != 0u && child_pid != 0u) {
        for (size_t i = 0u; i < UTAMO_PROCESS_LIMIT; ++i) {
            if (registry[i] != NULL && registry[i]->pid == child_pid &&
                registry[i]->parent_pid == parent_pid) {
                result = UTAMO_SYS_EAGAIN;
            }
        }
        for (size_t i = 0u; i < UTAMO_PROCESS_HISTORY; ++i) {
            if (history[i].pid == child_pid && history[i].parent_pid == parent_pid &&
                !history[i].collected) {
                if (user_vm_copy_to(&parent->vm, status_address,
                                     &history[i].exit_code, sizeof(history[i].exit_code))) {
                    result = 0;
                    history[i].collected = true;
                } else {
                    result = UTAMO_SYS_EFAULT;
                }
                break;
            }
        }
    }
    cpu_irq_restore(flags);
    return result;
}
