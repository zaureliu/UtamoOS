# VFS and initramfs

GREEN local v0.6.0; see [gate evidence](validation-astra-v0.6.json).

The root filesystem is an immutable RAM view of a Limine module labelled
`utamo-initramfs`. Module bytes remain reserved for the lifetime of the kernel.
The boot adapter validates its HHDM extent, refuses missing/duplicate modules
and caps the image at 4 MiB. Limine v8.7.0 and the existing toolchain are unchanged.

`ramfs_import` builds an unpublished, bounded index (128 nodes, 128-byte paths).
It validates the entire archive before the root can be mounted. A failed import
clears the index. File data references the reserved archive: no writable aliases
are exposed to users, and there is no unmount or concurrent namespace mutation.
The root contains real `/bin`, `/dev` (initially empty) and `/etc` directories.

The accepted format is the uncompressed `070701` subset of
[CPIO newc](https://www.kernel.org/doc/html/latest/driver-api/early-userspace/buffer-format.html).
Every field must be hexadecimal; bounds, alignment, NUL termination, duplicates
and parent-directory ownership are checked. Regular files and directories only;
hardlinks, symlinks, devices, CRC/compression and concatenated archives are
unsupported. Parents precede children; root is implicit. A trailer is required,
with zero data and only zero padding afterward. These are deliberate restrictions,
not claims of implementing every CPIO variant.

Paths are absolute, printable ASCII, with no dot/dot-dot components, backslashes,
repeated slashes or trailing slash (except root). Failed opens preserve handles.
Reads are bounded by the real file size; EOF returns zero. Absolute seek can
reach EOF but not pass it. A close invalidates the handle. Directory enumeration
reports immediate children. The filesystem currently exposes read-only files;
there is no persistence, POSIX layer, ownership enforcement or device filesystem.

Each process owns 16 descriptor slots. Slots 1/2 retain WRITE console semantics;
0 has no input implementation. Files use 3–15. Each open has an independent offset.
READ first uses a tentative handle, then copies to validated user pages, and
commits the offset only after copying succeeds. Copies are at most 1024 bytes.
The process's descriptor table disappears during deferred reaping; immutable
nodes need no per-open allocation or references to a dying process.

Kernel recovery commands: `ls [path]`, `cat path` (4096-byte display limit),
`exec path [one argument string]`, and `fstest`. No user input syscall or
userspace shell is claimed. The existing keyboard consumer and kernel shell remain.

`make initramfs` packs separately linked ELF files and fixed /etc content using
Python's standard library. Archive order, inode numbers, modes, uid/gid and
timestamps are deterministic. `make iso` places that archive on the ISO.

## v0.7 extension

The root initramfs remains immutable. Up to three additional verified top-level
subtrees can be published before PID 1, without shadowing or unmounting.
Readonly FAT32 is imported through block reads into a bounded cache at /disk;
open descriptors keep stable nodes and use the existing offset/copy contracts.
See [storage](storage.md) for ownership and format limits.
