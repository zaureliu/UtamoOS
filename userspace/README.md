# UTAMO native userspace

The v0.6 candidate contains a freestanding runtime, native INT128 wrappers,
a dedicated ELF linker script and six separate programs under bin/.
Build with `make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" userspace`.
`make initramfs` packages them; `make iso` also generates the required archive.

See [the userspace ABI](../docs/userspace.md), [ELF loading](../docs/elf-loader.md)
and [the VFS](../docs/vfs.md). These programs do not link the host libc or use
Linux syscalls. The shell remains the existing kernel recovery interface.
