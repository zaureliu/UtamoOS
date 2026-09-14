/* SPDX-License-Identifier: MIT */
#include <utamo/process.h>
#include <utamo/syscall_abi.h>
#include <utamo/string.h>
#include <utamo/pit.h>
#include <utamo/pmm.h>
#include <utamo/elf.h>

static bool path_copy(struct process *process, uint64_t address, uint64_t length,
                      char *path)
{
    if (length == 0u || length >= UTAMO_VFS_PATH_MAX ||
        !user_vm_copy_from(&process->vm, path, address, (size_t)length)) {
        return false;
    }
    for (size_t i = 0u; i < (size_t)length; ++i) {
        if (path[i] == '\0') {
            return false;
        }
    }
    path[length] = '\0';
    return vfs_path_valid(path);
}

static bool argument_copy(struct process *process, uint64_t address, char *argument)
{
    if (address == 0u) {
        argument[0] = '\0';
        return true;
    }
    for (size_t i = 0u; i < UTAMO_EXEC_ARG_MAX; ++i) {
        if (address > UINT64_MAX - i ||
            !user_vm_copy_from(&process->vm, argument + i, address + i, 1u)) {
            return false;
        }
        if (argument[i] == '\0') {
            return true;
        }
    }
    return false;
}

int64_t process_file_syscall(struct process *process, uint64_t number,
                             uint64_t a, uint64_t b, uint64_t c)
{
    char path[UTAMO_VFS_PATH_MAX];
    switch (number) {
    case UTAMO_SYS_OPEN: {
        if (!path_copy(process, a, b, path)) {
            return UTAMO_SYS_EFAULT;
        }
        if (c != 0u) {
            return UTAMO_SYS_EINVAL; /* Read-only flags = zero. */
        }
        for (size_t fd = 3u; fd < UTAMO_PROCESS_FD_LIMIT; ++fd) {
            if (process->files[fd].node == NULL) {
                return vfs_open(path, &process->files[fd]) ?
                    (int64_t)fd : UTAMO_SYS_ENOENT;
            }
        }
        return UTAMO_SYS_EMFILE;
    }
    case UTAMO_SYS_READ: {
        if (a < 3u || a >= UTAMO_PROCESS_FD_LIMIT || process->files[a].node == NULL) {
            return UTAMO_SYS_EBADF;
        }
        if (c > UTAMO_SYS_WRITE_LIMIT) {
            return UTAMO_SYS_EINVAL;
        }
        struct vfs_file tentative = process->files[a];
        char buffer[UTAMO_SYS_WRITE_LIMIT];
        size_t bytes = 0u;
        if (!vfs_read(&tentative, buffer, (size_t)c, &bytes)) {
            return UTAMO_SYS_EIO;
        }
        if (!user_vm_copy_to(&process->vm, b, buffer, bytes)) {
            return UTAMO_SYS_EFAULT;
        }
        process->files[a] = tentative; /* Commit offset only after a complete copy. */
        return (int64_t)bytes;
    }
    case UTAMO_SYS_CLOSE:
        if (a < 3u || a >= UTAMO_PROCESS_FD_LIMIT || !vfs_close(&process->files[a])) {
            return UTAMO_SYS_EBADF;
        }
        return 0;
    case UTAMO_SYS_SEEK:
        if (a < 3u || a >= UTAMO_PROCESS_FD_LIMIT || process->files[a].node == NULL) {
            return UTAMO_SYS_EBADF;
        }
        return c == 0u && vfs_seek(&process->files[a], (size_t)b) ?
            (int64_t)b : UTAMO_SYS_EINVAL;
    case UTAMO_SYS_FSTAT:
        if (a < 3u || a >= UTAMO_PROCESS_FD_LIMIT || process->files[a].node == NULL) {
            return UTAMO_SYS_EBADF;
        }
        return (int64_t)process->files[a].node->size;
    case UTAMO_SYS_SPAWN: {
        char argument[UTAMO_EXEC_ARG_MAX];
        uint64_t pid = 0u;
        if (!path_copy(process, a, b, path) || !argument_copy(process, c, argument)) {
            return UTAMO_SYS_EFAULT;
        }
        return process_spawn_elf(path, argument, process->pid, &pid) ?
            (int64_t)pid : UTAMO_SYS_EEXEC;
    }
    case UTAMO_SYS_WAIT:
        return process_collect_child(process, a, b);
    case UTAMO_SYS_INFO: {
        struct pmm_stats stats;
        if (b != sizeof(struct utamo_system_info) || !pmm_get_stats(&stats)) {
            return UTAMO_SYS_EINVAL;
        }
        const struct utamo_system_info info = {
            .pid = process->pid, .ticks = pit_get_ticks(),
            .free_pages = stats.free_frames, .page_size = 4096u
        };
        return user_vm_copy_to(&process->vm, a, &info, sizeof(info)) ? 0 : UTAMO_SYS_EFAULT;
    }
    default:
        return UTAMO_SYS_ENOSYS;
    }
}
