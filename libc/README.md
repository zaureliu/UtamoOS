# Library scope

The minimal native userspace runtime lives in
[userspace/libc](../userspace/libc/runtime.c). It supplies UTAMO-specific syscall
wrappers and small output helpers. This directory does not contain a POSIX libc,
glibc port or a second competing implementation.
