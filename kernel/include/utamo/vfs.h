/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_VFS_H
#define UTAMO_VFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTAMO_VFS_PATH_MAX 128u
#define UTAMO_VFS_NODE_LIMIT 128u
#define UTAMO_INITRAMFS_LIMIT (4u * 1024u * 1024u)
enum vfs_type { UTAMO_VFS_FILE = 1, UTAMO_VFS_DIRECTORY = 2 };
struct vfs_node {
    char path[UTAMO_VFS_PATH_MAX];
    const unsigned char *data;
    size_t size;
    enum vfs_type type;
    uint32_t mode;
};
struct ramfs {
    struct vfs_node nodes[UTAMO_VFS_NODE_LIMIT];
    size_t count;
    bool ready;
};
struct vfs_file {
    const struct vfs_node *node;
    size_t offset;
};
/* Strict canonical absolute paths: no dot components, repeated/trailing slash.
 * All strings/objects are trusted kernel buffers; user ABI copies first. */
bool vfs_path_valid(const char *path);
/* Parse an immutable newc image into an unpublished zeroed fs. No allocation.
 * Failure clears fs; backing image must outlive fs and all open handles. */
bool ramfs_import(struct ramfs *fs, const void *image, size_t size);
const struct vfs_node *ramfs_lookup(const struct ramfs *fs, const char *path);
bool vfs_mount_root(const struct ramfs *fs); /* Once, before process publication. */
const struct vfs_node *vfs_lookup(const char *path);
bool vfs_open(const char *path, struct vfs_file *file);
bool vfs_read(struct vfs_file *file, void *buffer, size_t bytes, size_t *read);
bool vfs_seek(struct vfs_file *file, size_t offset);
bool vfs_close(struct vfs_file *file);
const struct vfs_node *vfs_child(const char *directory, size_t index);
#endif
