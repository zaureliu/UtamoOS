# Filesystem subsystem

The v0.6 candidate uses a bounded immutable newc index and a read-only VFS.
The boot module stays reserved. File handles have independent offsets; native
processes own descriptor tables and user copies remain checked.

See [VFS ownership, format and limits](../../docs/vfs.md).
Disk-backed storage is a later gated milestone.
