# Filesystem subsystem

The immutable newc index provides the root VFS. The boot module stays
reserved. File handles have independent offsets; native processes own
private descriptor tables and checked user copies.

The v0.7 storage gate added a bounded readonly FAT32 snapshot mounted at
/disk after real AHCI reads. Mounted data outlives every descriptor. Failed
imports release unpublished state. There is no write or unmount API.

See [VFS ownership and limits](../../docs/vfs.md),
[storage contracts](../../docs/storage.md) and
[campaign evidence](../../docs/astra-campaign-state.md).
