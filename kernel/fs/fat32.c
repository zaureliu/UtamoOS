/* SPDX-License-Identifier: MIT */
#include <utamo/fat32.h>
#include <utamo/string.h>
static uint16_t get16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u));
}
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2u) << 16u);
}
static bool sector(struct fat32 *fs, uint64_t lba, void *out)
{
    /* Total work budget includes all metadata and data, not just recursion. */
    if (++fs->reads > 65536u) { return false; }
    return block_read(fs->device, lba, 1u, out);
}
static bool entry(struct fat32 *fs, uint32_t cluster, uint32_t *next)
{
    const uint32_t relative = cluster / 128u;
    if (relative >= fs->fat_sectors) { return false; }
    if (!fs->cache_valid || fs->cached_sector != relative) {
        const uint64_t lba = fs->fat_start + (uint64_t)fs->active_fat * fs->fat_sectors + relative;
        if (!sector(fs, lba, fs->fat_cache)) { return false; }
        if (fs->mirrored && fs->fats == 2u) {
            unsigned char mirror[512];
            if (!sector(fs, (uint64_t)fs->fat_start + fs->fat_sectors + relative, mirror) ||
                memcmp(mirror, fs->fat_cache, 512u) != 0) { return false; }
        }
        fs->cache_valid = true; fs->cached_sector = relative;
    }
    *next = get32(fs->fat_cache + (cluster % 128u) * 4u) & UINT32_C(0x0fffffff);
    return true;
}
static bool valid_cluster(const struct fat32 *fs, uint32_t cluster)
{
    return cluster >= 2u && cluster - 2u < fs->clusters;
}
static bool claim(struct fat32 *fs, uint32_t cluster)
{
    if (!valid_cluster(fs, cluster) || ++fs->visited > 16384u) { return false; }
    const uint32_t index = cluster - 2u;
    const unsigned char bit = (unsigned char)(1u << (index & 7u));
    if ((fs->claimed[index / 8u] & bit) != 0u) { return false; }
    fs->claimed[index / 8u] |= bit;
    return true;
}
static uint64_t cluster_lba(const struct fat32 *fs, uint32_t cluster)
{
    return fs->data_start + (uint64_t)(cluster - 2u) * fs->sectors_per_cluster;
}
static bool short_name(const unsigned char *raw, char out[13])
{
    size_t used = 0u;
    for (size_t part = 0u; part < 2u; ++part) {
        const size_t begin = part == 0u ? 0u : 8u, end = part == 0u ? 8u : 11u;
        bool padding = false, wrote = false;
        for (size_t i = begin; i < end; ++i) {
            const unsigned char c = raw[i];
            if (c == ' ') { padding = true; continue; }
            if (padding || c < 33u || c > 126u ||
                c == '"' || c == '*' || c == '+' || c == ',' || c == '.' ||
                c == '/' || c == ':' || c == ';' || c == '<' || c == '=' ||
                c == '>' || c == '?' || c == '[' || c == '\\' || c == ']' || c == '|') {
                return false;
            }
            if (part == 1u && !wrote) { out[used++] = '.'; }
            /* Case-normalize ASCII short names, making collisions explicit. */
            out[used++] = (char)(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
            wrote = true;
        }
        if (part == 0u && !wrote) { return false; }
    }
    out[used] = '\0';
    return true;
}
static bool read_file(struct fat32 *fs, struct vfs_node *node, uint32_t cluster)
{
    if (node->size == 0u) { return cluster == 0u; }
    if (node->size > UTAMO_FAT32_FILE_MAX ||
        node->size > UTAMO_FAT32_CACHE_MAX - fs->cached_bytes) { return false; }
    unsigned char *data = fs->memory.alloc(fs->memory.context, node->size);
    if (data == NULL) { return false; }
    node->data = data; fs->cached_bytes += node->size;
    size_t copied = 0u;
    for (;;) {
        if (!claim(fs, cluster)) { return false; }
        for (uint32_t i = 0u; i < fs->sectors_per_cluster && copied < node->size; ++i) {
            unsigned char block[512];
            if (!sector(fs, cluster_lba(fs, cluster) + i, block)) { return false; }
            const size_t left = node->size - copied, bytes = left < 512u ? left : 512u;
            memcpy(data + copied, block, bytes); copied += bytes;
        }
        uint32_t next;
        if (!entry(fs, cluster, &next)) { return false; }
        if (copied == node->size) { return next >= UINT32_C(0x0ffffff8); }
        if (!valid_cluster(fs, next)) { return false; }
        cluster = next;
    }
}
static bool directory(struct fat32 *fs, uint32_t cluster, uint32_t parent,
                       const char *path, unsigned int depth)
{
    if (depth > 8u) { return false; }
    const uint32_t first = cluster;
    bool ended = false;
    for (;;) {
        if (!claim(fs, cluster)) { return false; }
        for (uint32_t s = 0u; s < fs->sectors_per_cluster && !ended; ++s) {
            unsigned char data[512];
            if (!sector(fs, cluster_lba(fs, cluster) + s, data)) { return false; }
            for (size_t i = 0u; i < 512u; i += 32u) {
                const unsigned char *e = data + i;
                if (e[0] == 0u) { ended = true; break; }
                if (e[0] == 0xe5u || e[11] == 0x0fu) { continue; }
                if ((e[11] & 0xc0u) != 0u) { return false; }
                if ((e[11] & 8u) != 0u) {
                    if ((e[11] & 16u) != 0u) { return false; }
                    continue; /* Volume label is not a file. */
                }
                const uint32_t child = (uint32_t)get16(e + 26u) | ((uint32_t)get16(e + 20u) << 16u);
                const uint32_t size = get32(e + 28u);
                const bool dir = (e[11] & 16u) != 0u;
                if (e[0] == '.') {
                    const bool dot = memcmp(e, ".          ", 11u) == 0;
                    const bool dotdot = memcmp(e, "..         ", 11u) == 0;
                    const uint32_t expected = dot ? first : parent;
                    if ((!dot && !dotdot) || !dir || size != 0u ||
                        child != expected) { return false; }
                    continue;
                }
                char name[13];
                if (!short_name(e, name) || fs->tree.count == UTAMO_VFS_NODE_LIMIT ||
                    (dir && size != 0u)) { return false; }
                const size_t prefix = strlen(path), length = strlen(name);
                if (prefix + 1u + length >= UTAMO_VFS_PATH_MAX) { return false; }
                struct vfs_node *node = &fs->tree.nodes[fs->tree.count];
                memcpy(node->path, path, prefix); node->path[prefix] = '/';
                memcpy(node->path + prefix + 1u, name, length + 1u);
                if (!vfs_path_valid(node->path) || ramfs_lookup(&fs->tree, node->path) != NULL) {
                    return false;
                }
                node->size = size;
                node->type = dir ? UTAMO_VFS_DIRECTORY : UTAMO_VFS_FILE;
                node->mode = dir ? 0040555u : 0100444u;
                ++fs->tree.count; /* Own node/data before any subsequent failure. */
                if (dir) {
                    if (!directory(fs, child, first == fs->root_cluster ? 0u : first,
                                    node->path, depth + 1u)) { return false; }
                } else if (!read_file(fs, node, child)) { return false; }
            }
        }
        uint32_t next;
        if (!entry(fs, cluster, &next)) { return false; }
        if (next >= UINT32_C(0x0ffffff8)) { return true; }
        if (!valid_cluster(fs, next)) { return false; }
        cluster = next;
    }
}
void fat32_discard(struct fat32 *fs)
{
    if (fs == NULL) { return; }
    if (fs->memory.free != NULL) {
        for (size_t i = 0u; i < fs->tree.count; ++i) {
            if (fs->tree.nodes[i].data != NULL) {
                fs->memory.free(fs->memory.context, (void *)(uintptr_t)fs->tree.nodes[i].data);
            }
        }
        if (fs->claimed != NULL) { fs->memory.free(fs->memory.context, fs->claimed); }
    }
    memset(fs, 0, sizeof(*fs));
}
bool fat32_import(struct fat32 *fs, const struct block_device *device,
                   const struct fat32_alloc *memory)
{
    if (fs == NULL || fs->tree.count != 0u || fs->tree.ready || fs->device != NULL ||
        device == NULL || device->sector_size != 512u || memory == NULL ||
        memory->alloc == NULL || memory->free == NULL) { return false; }
    fs->device = device; fs->memory = *memory;
    unsigned char boot[512];
    if (!sector(fs, 0u, boot)) { goto fail; }
    const uint16_t reserved = get16(boot + 14u), flags = get16(boot + 40u);
    const uint32_t total = get32(boot + 32u), fat_size = get32(boot + 36u);
    const uint8_t spc = boot[13], fats = boot[16];
    const uint64_t overhead = reserved + (uint64_t)fats * fat_size;
    if (boot[510] != 0x55u || boot[511] != 0xaau || get16(boot + 11u) != 512u ||
        spc == 0u || spc > 128u || (spc & (spc - 1u)) != 0u || reserved == 0u ||
        (fats != 1u && fats != 2u) || get16(boot + 17u) != 0u ||
        get16(boot + 19u) != 0u || get16(boot + 22u) != 0u || get16(boot + 42u) != 0u ||
        get32(boot + 28u) != 0u || boot[21] < 0xf0u || fat_size == 0u ||
        overhead >= total || total > device->sectors || (flags & 0xff70u) != 0u) { goto fail; }
    fs->clusters = (uint32_t)((total - overhead) / spc);
    if (fs->clusters < 65525u || fs->clusters > UTAMO_FAT32_CLUSTER_MAX ||
        (uint64_t)fat_size * 128u < (uint64_t)fs->clusters + 2u) { goto fail; }
    fs->mirrored = (flags & 0x80u) == 0u;
    fs->active_fat = fs->mirrored ? 0u : (uint8_t)(flags & 15u);
    if (fs->active_fat >= fats) { goto fail; }
    fs->fat_start = reserved; fs->fat_sectors = fat_size;
    fs->data_start = (uint32_t)overhead; fs->sectors_per_cluster = spc; fs->fats = fats;
    fs->root_cluster = get32(boot + 44u);
    uint32_t first, second;
    if (!valid_cluster(fs, fs->root_cluster) || !entry(fs, 0u, &first) ||
        !entry(fs, 1u, &second) || first != (UINT32_C(0x0fffff00) | boot[21]) ||
        second < UINT32_C(0x0ffffff8)) { goto fail; }
    const size_t bitmap = ((size_t)fs->clusters + 7u) / 8u;
    fs->claimed = fs->memory.alloc(fs->memory.context, bitmap);
    if (fs->claimed == NULL) { goto fail; }
    memset(fs->claimed, 0, bitmap);
    memcpy(fs->tree.nodes[0].path, "/disk", 6u);
    fs->tree.nodes[0].type = UTAMO_VFS_DIRECTORY;
    fs->tree.nodes[0].mode = 0040555u; fs->tree.count = 1u;
    if (!directory(fs, fs->root_cluster, 0u, "/disk", 0u)) { goto fail; }
    fs->memory.free(fs->memory.context, fs->claimed); fs->claimed = NULL;
    fs->tree.ready = true;
    return true;
fail:
    fat32_discard(fs);
    return false;
}
