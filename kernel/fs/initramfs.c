/* SPDX-License-Identifier: MIT */
#include <utamo/vfs.h>
#include <utamo/string.h>

static bool hex8(const unsigned char *text, uint32_t *out)
{
    uint32_t value = 0u;
    for (size_t i = 0u; i < 8u; ++i) {
        unsigned int digit = text[i];
        if (digit >= '0' && digit <= '9') {
            digit -= '0';
        } else if (digit >= 'a' && digit <= 'f') {
            digit = digit - 'a' + 10u;
        } else if (digit >= 'A' && digit <= 'F') {
            digit = digit - 'A' + 10u;
        } else {
            return false;
        }
        value = (value << 4u) | digit;
    }
    *out = value;
    return true;
}

static bool parse(struct ramfs *fs, const unsigned char *image, size_t size)
{
    fs->nodes[0] = (struct vfs_node){
        .path = "/", .type = UTAMO_VFS_DIRECTORY, .mode = 0755u
    };
    fs->count = 1u;
    size_t offset = 0u;
    while (offset <= size && size - offset >= 110u) {
        const unsigned char *header = image + offset;
        uint32_t fields[13];
        if (memcmp(header, "070701", 6u) != 0) {
            return false;
        }
        for (size_t i = 0u; i < 13u; ++i) {
            if (!hex8(header + 6u + i * 8u, &fields[i])) {
                return false;
            }
        }
        const size_t namesize = fields[11], bytes = fields[6];
        const uint32_t type = fields[1] & 0170000u;
        offset += 110u;
        if (namesize < 2u || namesize >= UTAMO_VFS_PATH_MAX ||
            namesize > size - offset || fields[12] != 0u) {
            return false;
        }
        const unsigned char *name = image + offset;
        if (name[namesize - 1u] != 0u) {
            return false;
        }
        for (size_t i = 0u; i + 1u < namesize; ++i) {
            if (name[i] == 0u) {
                return false;
            }
        }
        offset = (offset + namesize + 3u) & ~(size_t)3u;
        if (offset > size || bytes > size - offset) {
            return false;
        }
        if (namesize == sizeof("TRAILER!!!") &&
            memcmp(name, "TRAILER!!!", sizeof("TRAILER!!!")) == 0) {
            if (bytes != 0u) {
                return false;
            }
            for (size_t i = offset; i < size; ++i) {
                if (image[i] != 0u) {
                    return false;
                }
            }
            return true;
        }
        if ((type != 0100000u && type != 0040000u) ||
            (type == 0040000u && bytes != 0u) ||
            fields[4] == 0u || (type == 0100000u && fields[4] != 1u) ||
            fs->count == UTAMO_VFS_NODE_LIMIT || name[0] == '/') {
            return false;
        }
        struct vfs_node node = {
            .type = type == 0100000u ? UTAMO_VFS_FILE : UTAMO_VFS_DIRECTORY,
            .data = image + offset, .size = bytes, .mode = fields[1] & 0777u
        };
        node.path[0] = '/';
        memcpy(node.path + 1u, name, namesize);
        if (!vfs_path_valid(node.path) || ramfs_lookup(fs, node.path) != NULL) {
            return false;
        }
        char parent[UTAMO_VFS_PATH_MAX];
        memcpy(parent, node.path, namesize + 1u);
        size_t slash = namesize - 1u;
        while (slash > 0u && parent[slash] != '/') {
            --slash;
        }
        parent[slash == 0u ? 1u : slash] = '\0';
        const struct vfs_node *directory = ramfs_lookup(fs, parent);
        if (directory == NULL || directory->type != UTAMO_VFS_DIRECTORY) {
            return false;
        }
        fs->nodes[fs->count++] = node;
        offset = (offset + bytes + 3u) & ~(size_t)3u;
    }
    return false; /* A trailer is mandatory for this bounded archive subset. */
}

bool ramfs_import(struct ramfs *fs, const void *image, size_t size)
{
    if (fs == NULL || fs->ready || fs->count != 0u) {
        return false;
    }
    if (image == NULL || size > UTAMO_INITRAMFS_LIMIT || !parse(fs, image, size)) {
        memset(fs, 0, sizeof(*fs));
        return false;
    }
    fs->ready = true;
    return true;
}
