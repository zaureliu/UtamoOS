/* SPDX-License-Identifier: MIT */
#include <utamo/vfs.h>
#include <utamo/string.h>
static const struct ramfs *root;

bool vfs_path_valid(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return false;
    }
    size_t length = 0u;
    while (length < UTAMO_VFS_PATH_MAX && path[length] != '\0') {
        ++length;
    }
    if (length == UTAMO_VFS_PATH_MAX) {
        return false;
    }
    if (length == 1u) {
        return true;
    }
    size_t start = 1u;
    for (size_t i = 1u; i <= length; ++i) {
        const unsigned char c = (unsigned char)path[i];
        if (c != 0u && (c < 32u || c > 126u || c == '\\')) {
            return false;
        }
        if (c == '/' || c == 0u) {
            const size_t part = i - start;
            if (part == 0u || (part == 1u && path[start] == '.') ||
                (part == 2u && path[start] == '.' && path[start + 1u] == '.')) {
                return false;
            }
            start = i + 1u;
        }
    }
    return true;
}

const struct vfs_node *ramfs_lookup(const struct ramfs *fs, const char *path)
{
    if (fs == NULL || fs->count > UTAMO_VFS_NODE_LIMIT || !vfs_path_valid(path)) {
        return NULL;
    }
    for (size_t i = 0u; i < fs->count; ++i) {
        if (strcmp(fs->nodes[i].path, path) == 0) {
            return &fs->nodes[i];
        }
    }
    return NULL;
}

bool vfs_mount_root(const struct ramfs *fs)
{
    if (root != NULL || fs == NULL || !fs->ready || fs->count == 0u ||
        fs->count > UTAMO_VFS_NODE_LIMIT) {
        return false;
    }
    root = fs;
    return true;
}

const struct vfs_node *vfs_lookup(const char *path)
{
    return ramfs_lookup(root, path);
}

bool vfs_open(const char *path, struct vfs_file *file)
{
    if (file == NULL || file->node != NULL) {
        return false;
    }
    const struct vfs_node *node = vfs_lookup(path);
    if (node == NULL || node->type != UTAMO_VFS_FILE) {
        return false;
    }
    *file = (struct vfs_file){.node = node};
    return true;
}

bool vfs_read(struct vfs_file *file, void *buffer, size_t bytes, size_t *read)
{
    if (file == NULL || file->node == NULL || read == NULL ||
        (bytes != 0u && buffer == NULL) || file->offset > file->node->size) {
        return false;
    }
    const size_t left = file->node->size - file->offset;
    const size_t count = bytes < left ? bytes : left;
    if (count != 0u) {
        memcpy(buffer, file->node->data + file->offset, count);
    }
    file->offset += count;
    *read = count;
    return true;
}

bool vfs_seek(struct vfs_file *file, size_t offset)
{
    if (file == NULL || file->node == NULL || offset > file->node->size) {
        return false;
    }
    file->offset = offset;
    return true;
}

bool vfs_close(struct vfs_file *file)
{
    if (file == NULL || file->node == NULL) {
        return false;
    }
    *file = (struct vfs_file){0};
    return true;
}

const struct vfs_node *vfs_child(const char *directory, size_t index)
{
    const struct vfs_node *parent = vfs_lookup(directory);
    if (parent == NULL || parent->type != UTAMO_VFS_DIRECTORY) {
        return NULL;
    }
    const size_t prefix = strlen(directory);
    for (size_t i = 1u; i < root->count; ++i) {
        const char *path = root->nodes[i].path;
        if (strncmp(path, directory, prefix) != 0 ||
            (prefix != 1u && path[prefix] != '/')) {
            continue;
        }
        const char *name = path + prefix + (prefix == 1u ? 0u : 1u);
        if (*name == '\0') {
            continue;
        }
        size_t j = 0u;
        while (name[j] != '\0' && name[j] != '/') {
            ++j;
        }
        if (name[j] == '\0' && index-- == 0u) {
            return &root->nodes[i];
        }
    }
    return NULL;
}
