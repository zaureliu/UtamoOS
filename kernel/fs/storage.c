/* SPDX-License-Identifier: MIT */
#include <utamo/storage.h>
#include <utamo/pci.h>
#include <utamo/ahci.h>
#include <utamo/fat32.h>
#include <utamo/heap.h>
#include <utamo/pmm.h>
#include <utamo/log.h>
#include <utamo/panic.h>
#include <utamo/string.h>
static struct fat32 mounted;
static bool attempted;
static void *allocate(void *context, size_t bytes) { (void)context; return kmalloc(bytes); }
static void release(void *context, void *pointer)
{
    (void)context;
    if (!kfree(pointer)) { PANIC("FAT32 cache ownership mismatch"); }
}
static const struct fat32_alloc allocator = {.alloc = allocate, .free = release};
void storage_init(void)
{
    if (attempted) { return; }
    attempted = true;
    if (!pci_init()) { LOG_WARN("PCI inventory rejected"); return; }
    LOG_OK("PCI enumeration: %llu devices", (unsigned long long)pci_count());
    const int result = ahci_init();
    if (result < 0) { LOG_WARN("AHCI initialization rejected"); return; }
    if (result == 0) { return; }
    if (!fat32_import(&mounted, ahci_device(), &allocator)) {
        LOG_WARN("FAT32 rejected; /disk not mounted");
        return;
    }
    if (!vfs_mount_subtree(&mounted.tree)) {
        fat32_discard(&mounted);
        LOG_WARN("FAT32 namespace rejected; /disk not mounted");
        return;
    }
    LOG_OK("FAT32 mounted readonly at /disk: nodes=%llu cached_bytes=%llu",
        (unsigned long long)mounted.tree.count, (unsigned long long)mounted.cached_bytes);
}
void storage_status(void)
{
    ahci_status();
    kprintf("FAT32 mounted=%s nodes=%llu cached_bytes=%llu clusters=%u metadata_reads=%u\n",
        (const char *)(mounted.tree.ready ? "Yes" : "No"),
        (unsigned long long)mounted.tree.count, (unsigned long long)mounted.cached_bytes,
        mounted.clusters, mounted.reads);
}
static bool compare_snapshot(struct fat32 *copy)
{
    if (!fat32_import(copy, ahci_device(), &allocator)) { return false; }
    bool good = copy->tree.count == mounted.tree.count &&
        copy->cached_bytes == mounted.cached_bytes;
    for (size_t i = 0u; good && i < copy->tree.count; ++i) {
        const struct vfs_node *a = &copy->tree.nodes[i], *b = &mounted.tree.nodes[i];
        good = strcmp(a->path, b->path) == 0 && a->type == b->type && a->size == b->size &&
            (a->size == 0u || memcmp(a->data, b->data, a->size) == 0);
    }
    fat32_discard(copy);
    return good;
}
bool storage_selftest(void)
{
    if (!mounted.tree.ready || ahci_device() == NULL) { return false; }
    struct fat32 *copy = kcalloc(1u, sizeof(*copy));
    if (copy == NULL) { return false; }
    bool good = compare_snapshot(copy); /* Warm retained heap pages. */
    struct heap_stats before, after;
    struct pmm_stats pbefore, pafter;
    good = good && heap_get_stats(&before) && pmm_get_stats(&pbefore);
    for (unsigned int i = 0u; good && i < 8u; ++i) { good = compare_snapshot(copy); }
    good = good && heap_get_stats(&after) && pmm_get_stats(&pafter) &&
        before.live_allocations == after.live_allocations && before.used_bytes == after.used_bytes &&
        pbefore.free_frames == pafter.free_frames && heap_validate();
    unsigned char sentinel[512];
    memset(sentinel, 0xa5, sizeof(sentinel));
    good = good && !block_read(ahci_device(), ahci_device()->sectors, 1u, sentinel) &&
        !block_read(ahci_device(), UINT64_MAX, 2u, sentinel) &&
        !block_read(ahci_device(), 0u, 0u, sentinel);
    for (size_t i = 0u; i < sizeof(sentinel); ++i) { good = good && sentinel[i] == 0xa5u; }
    fat32_discard(copy);
    release(NULL, copy);
    kprintf("Storage stress: rounds=8 snapshot_equal=%s accounting=%s\n",
        (const char *)(good ? "Yes" : "No"), (const char *)(good ? "PASS" : "FAIL"));
    return good;
}
