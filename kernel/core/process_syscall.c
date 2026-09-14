/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/scheduler.h>
#include <utamo/syscall_abi.h>
#include <utamo/log.h>
#include <utamo/panic.h>

struct interrupt_frame *process_on_syscall(struct interrupt_frame *frame)
{
    struct process *const process = scheduler_current_process();
    if (process == NULL || (frame->cs & 3u) != 3u) {
        PANIC("Native syscall requires a live CPL3 process");
    }
    process_record_syscall(process);
    switch (frame->rax) {
    case UTAMO_SYS_WRITE: {
        if (frame->rdi != 1u && frame->rdi != 2u) {
            frame->rax = (uint64_t)(int64_t)UTAMO_SYS_EBADF;
            break;
        }
        if (frame->rdx > UTAMO_SYS_WRITE_LIMIT) {
            frame->rax = (uint64_t)(int64_t)UTAMO_SYS_EINVAL;
            break;
        }
        const size_t length = (size_t)frame->rdx;
        char buffer[UTAMO_SYS_WRITE_LIMIT];
        if (!user_vm_copy_from(&process->vm, buffer, frame->rsi, length)) {
            frame->rax = (uint64_t)(int64_t)UTAMO_SYS_EFAULT;
            break;
        }
        log_write(buffer, length);
        frame->rax = frame->rdx;
        break;
    }
    case UTAMO_SYS_EXIT:
        /* Preserve all 64 bits of the application's signed status. */
        process_mark_exit(process, (int64_t)frame->rdi, false, 0u, 0u, 0u);
        return scheduler_exit_user(frame);
    case UTAMO_SYS_GETPID:
        frame->rax = process->pid;
        break;
    case UTAMO_SYS_YIELD:
        frame->rax = 0u;
        return scheduler_yield_user(frame, 0u);
    case UTAMO_SYS_SLEEP:
        frame->rax = 0u;
        return scheduler_yield_user(frame, frame->rdi);
    default:
        frame->rax = (uint64_t)process_file_syscall(process, frame->rax,
            frame->rdi, frame->rsi, frame->rdx);
        break;
    }
    return scheduler_resume_user(frame);
}
