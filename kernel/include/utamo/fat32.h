/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_FAT32_H
#define UTAMO_FAT32_H
#include <utamo/vfs.h>
#include <utamo/block.h>
#define UTAMO_FAT32_CACHE_MAX (4u * 1024u * 1024u)
#define UTAMO_FAT32_FILE_MAX (1024u * 1024u)
#define UTAMO_FAT32_CLUSTER_MAX 1048576u
struct fat32_alloc {
    void *context;
    void *(*alloc)(void *context, size_t bytes);
    void (*free)(void *context, void *pointer);
};
struct fat32 {
    struct ramfs tree;
    const struct block_device *device;
    struct fat32_alloc memory;
    unsigned char *claimed;
    uint32_t clusters, fat_start, fat_sectors, data_start, root_cluster;
    uint32_t cached_sector, visited, reads;
    size_t cached_bytes;
    uint8_t sectors_per_cluster, fats, active_fat;
    bool mirrored, cache_valid;
    unsigned char fat_cache[512];
};
/* Zeroed unpublished output; whole-volume superfloppy at LBA0, FAT32 only.
 * Imports a bounded readonly 8.3 snapshot at /disk. All I/O/allocation failures
 * discard the entire unpublished tree. No borrowed buffers after success. */
bool fat32_import(struct fat32 *fs, const struct block_device *device,
                   const struct fat32_alloc *memory);
/* Only unpublished fs; a mounted snapshot must live forever. */
void fat32_discard(struct fat32 *fs);
#endif
