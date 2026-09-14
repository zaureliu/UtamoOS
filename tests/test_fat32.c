/* SPDX-License-Identifier: MIT */
#include <utamo/fat32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned int checks, failures, allocations, fail_alloc, reads, fail_read, live;
static struct { uint64_t offset; unsigned char value; } mutation;
static bool mutate;
static FILE *image;
static struct fat32 fs;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
static void *allocate(void *context, size_t bytes)
{
    (void)context;
    if (++allocations == fail_alloc) { return NULL; }
    void *p = malloc(bytes);
    if (p != NULL) { ++live; }
    return p;
}
static void release(void *context, void *pointer)
{
    (void)context;
    CHECK(pointer != NULL && live != 0u);
    --live; free(pointer);
}
static bool read_disk(void *context, uint64_t lba, size_t count, void *buffer)
{
    (void)context;
    if (++reads == fail_read) { return false; }
    if (lba > 131072u || count > 131072u - lba ||
        fseek(image, (long)(lba * 512u), SEEK_SET) != 0 ||
        fread(buffer, 512u, count, image) != count) { return false; }
    if (mutate && mutation.offset >= lba*512u && mutation.offset < (lba+count)*512u) {
        ((unsigned char *)buffer)[mutation.offset-lba*512u] = mutation.value;
    }
    return true;
}
static const struct block_device disk = {.sectors=131072u,.sector_size=512u,.read=read_disk};
static const struct fat32_alloc memory = {.alloc=allocate,.free=release};
static void reset(void) { allocations=0u; reads=0u; fail_alloc=0u; fail_read=0u; mutate=false; }
static void open_image(const char *name)
{
    if (image != NULL) { fclose(image); }
    image=fopen(name,"rb");
    if (image == NULL) { perror(name); exit(2); }
    reset();
}
static void rejected(void)
{
    const bool accepted = fat32_import(&fs,&disk,&memory);
    if (accepted) { printf("Unexpected acceptance: alloc=%u read=%u mutate=%u offset=%llu value=%u\n",
        fail_alloc, fail_read, mutate ? 1u : 0u, (unsigned long long)mutation.offset,
        (unsigned int)mutation.value); }
    CHECK(!accepted);
    CHECK(fs.tree.count==0u && !fs.tree.ready && fs.device==NULL && live==0u);
    fat32_discard(&fs);
}
int main(void)
{
    open_image("build/tests/fat32-good.img");
    CHECK(fat32_import(&fs,&disk,&memory));
    CHECK(fs.tree.count==6u && fs.tree.ready && fs.clusters==128992u);
    const unsigned int allocation_count=allocations, read_count=reads;
    CHECK(!fat32_import(&fs,&disk,&memory));
    const struct vfs_node *file=ramfs_lookup(&fs.tree,"/disk/FILE.TXT");
    CHECK(file!=NULL && file->size==sizeof("UTAMO readonly AHCI and FAT32\n")-1u && memcmp(file->data,"UTAMO readonly AHCI and FAT32\n",sizeof("UTAMO readonly AHCI and FAT32\n")-1u)==0);
    const struct vfs_node *chain=ramfs_lookup(&fs.tree,"/disk/CHAIN.BIN");
    CHECK(chain!=NULL && chain->size==2049u);
    if (chain!=NULL) {
        bool good=true;
        for(size_t i=0u;i<chain->size;++i) { good=good && chain->data[i]==(unsigned char)(i*17u+3u); }
        CHECK(good);
    }
    CHECK(ramfs_lookup(&fs.tree,"/disk/DOCS/README.TXT")!=NULL);
    CHECK(ramfs_lookup(&fs.tree,"/disk/EMPTY.TXT")->data==NULL);
    fat32_discard(&fs); CHECK(live==0u);
    for(unsigned int i=1u;i<=allocation_count;++i) { reset();fail_alloc=i;rejected(); }
    for(unsigned int i=1u;i<=read_count;++i) { reset();fail_read=i;rejected(); }
    const struct {uint64_t offset; unsigned char value;} bad[]={
        {11,1},{12,4},{13,0},{13,3},{13,255},{14,0},{16,0},{16,3},
        {17,1},{19,1},{22,1},{28,1},{35,255},{36,1},{37,0},{40,0x82},
        {41,1},{42,1},{44,0},{47,0x80},{510,0},{511,0},
        {2080u*512u+11u,0x40},{2080u*512u, '/'},{2080u*512u+26u,2u},
        {2080u*512u+29u,2u},{2080u*512u+64u+31u,255u},
        {2082u*512u+26u,0u},{2082u*512u+32u+26u,4u}
    };
    for(size_t i=0u;i<sizeof(bad)/sizeof(bad[0]);++i) {
        reset();mutate=true;mutation.offset=bad[i].offset;mutation.value=bad[i].value;rejected();
    }
    const char *variants[]={"bad-bpb","root-loop","file-loop","bad-cluster","cross-link","mirror-mismatch"};
    for(size_t i=0u;i<sizeof(variants)/sizeof(variants[0]);++i) {
        char path[128];(void)snprintf(path,sizeof(path),"build/tests/fat32-%s.img",variants[i]);
        open_image(path);rejected();
    }
    open_image("build/tests/fat32-good.img");
    uint32_t random=0x714ac3u;
    for(size_t i=0u;i<800u;++i) {
        reset();random=random*1664525u+1013904223u;
        mutate=true;mutation.offset=(i%3u==0u?2080u*512u:i%3u==1u?32u*512u:0u)+(random%512u);
        mutation.value=(unsigned char)(random>>24u);
        const bool good=fat32_import(&fs,&disk,&memory);
        CHECK(!good || (fs.tree.ready && fs.tree.count<=UTAMO_VFS_NODE_LIMIT &&
                       fs.cached_bytes<=UTAMO_FAT32_CACHE_MAX));
        fat32_discard(&fs);CHECK(live==0u);
    }
    reset();
    static struct ramfs root={.nodes={{.path="/",.type=UTAMO_VFS_DIRECTORY}},.count=1u,.ready=true};
    CHECK(vfs_mount_root(&root));
    CHECK(fat32_import(&fs,&disk,&memory));
    CHECK(vfs_mount_subtree(&fs.tree));CHECK(!vfs_mount_subtree(&fs.tree));
    CHECK(vfs_lookup("/disk")==vfs_child("/",0u));
    CHECK(vfs_child("/disk",0u)==vfs_lookup("/disk/FILE.TXT"));
    CHECK(vfs_child("/disk/DOCS",0u)==vfs_lookup("/disk/DOCS/README.TXT"));
    struct vfs_file handle={0}; unsigned char result[32];size_t got;
    CHECK(vfs_open("/disk/CHAIN.BIN",&handle) && vfs_seek(&handle,2040u));
    CHECK(vfs_read(&handle,result,sizeof(result),&got) && got==9u && result[8]==3u);
    CHECK(!vfs_seek(&handle,2050u) && handle.offset==2049u);
    CHECK(vfs_close(&handle));
    /* Process exits immediately: no use of the namespace after release. */
    fat32_discard(&fs);CHECK(live==0u);fclose(image);
    printf("FAT32 host tests: %u checks, %u failures\n",checks,failures);
    return failures==0u?0:1;
}
