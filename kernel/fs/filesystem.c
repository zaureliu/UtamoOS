/* SPDX-License-Identifier: MIT */
#include <utamo/filesystem.h>
#include <utamo/boot.h>
#include <utamo/elf.h>
#include <utamo/vfs.h>
#include <utamo/process.h>
#include <utamo/scheduler.h>
#include <utamo/pit.h>
#include <utamo/pmm.h>
#include <utamo/heap.h>
#include <utamo/log.h>
#include <utamo/string.h>
static struct ramfs root_fs;

bool filesystem_init(void)
{
    const void *image = NULL;
    size_t size = 0u;
    if (!boot_read_initramfs(&image, &size) || !ramfs_import(&root_fs, image, size) ||
        !vfs_mount_root(&root_fs)) {
        return false;
    }
    LOG_OK("VFS initramfs mounted: %llu nodes, %llu bytes",
           (unsigned long long)root_fs.count, (unsigned long long)size);
    return true;
}

static bool wait_process(uint64_t pid)
{
    const uint64_t start = pit_get_ticks();
    struct process_result result;
    while (pit_get_ticks() - start < 1000u) {
        thread_reap();
        if (process_get_result(pid, &result)) {
            return !result.faulted && result.exit_code == 0;
        }
        if (!thread_sleep_ms(10u)) {
            return false;
        }
    }
    return false;
}

bool filesystem_start_init(void)
{
    uint64_t pid = 0u;
    return process_spawn_elf("/bin/init", "", 0u, &pid) && pid == 1u &&
           wait_process(pid);
}

bool filesystem_run(const char *path, const char *argument)
{
    uint64_t pid = 0u;
    if (!process_spawn_elf(path, argument, 0u, &pid)) {
        return false;
    }
    const bool good = wait_process(pid);
    kprintf("ELF process PID %llu: %s\n", (unsigned long long)pid,
            (const char *)(good ? "exit 0" : "failed or timed out"));
    return good;
}

void filesystem_list(const char *path)
{
    const struct vfs_node *directory = vfs_lookup(path);
    if (directory == NULL || directory->type != UTAMO_VFS_DIRECTORY) {
        kprintf("Directory unavailable.\n");
        return;
    }
    for (size_t i = 0u; i < UTAMO_VFS_NODE_LIMIT; ++i) {
        const struct vfs_node *node = vfs_child(path, i);
        if (node == NULL) {
            break;
        }
        kprintf("%s %llu %s\n",
            (const char *)(node->type == UTAMO_VFS_DIRECTORY ? "dir" : "file"),
            (unsigned long long)node->size, (const char *)node->path);
    }
}

void filesystem_cat(const char *path)
{
    struct vfs_file file = {0};
    if (!vfs_open(path, &file)) {
        kprintf("File unavailable.\n");
        return;
    }
    char buffer[256];
    size_t read = 0u, remaining = 4096u;
    while (remaining != 0u &&
           vfs_read(&file, buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer), &read) &&
           read != 0u) {
        log_write(buffer, read);
        remaining -= read;
    }
    if (file.offset != file.node->size) {
        kprintf("\nOutput limited to 4096 bytes.\n");
    }
    (void)vfs_close(&file);
}

bool filesystem_selftest(void)
{
    if (!process_available()) {
        return false;
    }
    /* Warm metadata once; heap/kernel stack pages may be retained legitimately. */
    if (!filesystem_run("/bin/filetest", "") || !filesystem_run("/bin/badptr", "")) {
        return false;
    }
    struct pmm_stats pmm_before, pmm_after;
    struct heap_stats heap_before, heap_after;
    struct process_stats processes_before, processes_after;
    if (!pmm_get_stats(&pmm_before) || !heap_get_stats(&heap_before) ||
        !process_get_stats(&processes_before)) {
        return false;
    }
    for (size_t round = 0u; round < 8u; ++round) {
        uint64_t untouched = UINT64_MAX;
        if (process_spawn_elf("/etc/not-elf", "", 0u, &untouched) ||
            untouched != UINT64_MAX ||
            !filesystem_run("/bin/hello", "") ||
            !filesystem_run("/bin/filetest", "") ||
            !filesystem_run("/bin/badptr", "")) {
            return false;
        }
    }
    const bool good = pmm_get_stats(&pmm_after) && heap_get_stats(&heap_after) &&
        process_get_stats(&processes_after) && heap_validate() && scheduler_validate() &&
        pmm_after.used_frames == pmm_before.used_frames &&
        heap_after.used_bytes == heap_before.used_bytes &&
        heap_after.live_allocations == heap_before.live_allocations &&
        processes_after.active == processes_before.active &&
        processes_after.created - processes_before.created == 32u &&
        processes_after.reaped - processes_before.reaped == 32u &&
        processes_after.user_faults == processes_before.user_faults;
    kprintf("ELF/VFS stress: 8 rounds, 32 processes, exact PMM/heap restoration: %s\n",
            (const char *)(good ? "PASS" : "FAIL"));
    return good;
}
