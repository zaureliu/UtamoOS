# Native userspace runtime

GREEN local v0.6.0; [gate evidence](validation-astra-v0.6.json). All programs are separate freestanding x86_64 ELF files built
by the existing x86_64-elf GCC/NASM toolchain and placed in an immutable initramfs.
No host libc is linked.

| Program | Behavior |
| --- | --- |
| /bin/init | PID 1; launches three controlled demonstrations and waits |
| /bin/hello | Emits Hello from UTAMO userspace! using native WRITE |
| /bin/echo | Prints its copied native argument string |
| /bin/sysinfo | Reads actual PID, PIT ticks, free frames and page size |
| /bin/filetest | Checks BSS/data, VFS reads/seek/close/EOF, spawn/wait and cleanup |
| /bin/badptr | Checks invalid destinations, atomic file offsets and FD exhaustion |

The runtime supplies _start, INT128 wrappers, exit, puts, strlen, decimal output,
spawn and polling wait with scheduler sleep. It uses only general registers,
16-byte call alignment and the documented UTAMO ABI. The userspace shell remains
a stretch item: there is no robust input syscall yet.

Additional native syscalls (RAX number/result, RDI/RSI/RDX arguments):

| Number | Call | Arguments and result |
| ---: | --- | --- |
| 6 | OPEN | path address, byte length, flags=0; descriptor or error |
| 7 | READ | descriptor, destination, max bytes<=1024; bytes or error |
| 8 | CLOSE | descriptor; zero or EBADF |
| 9 | SEEK | descriptor, absolute byte offset, origin=0; offset or error |
| 10 | FSTAT | descriptor; immutable file size or EBADF |
| 11 | SPAWN | path address, byte length, optional NUL argument address; child PID or error |
| 12 | WAIT | child PID, destination for signed 64-bit status; zero, EAGAIN or error |
| 13 | INFO | destination, exact 32-byte structure size; zero or error |

INFO returns four uint64 fields: pid, ticks, free_pages, page_size. The layout
is shared by the kernel and native runtime. No kernel pointers are returned.
Numbers and errors are centralized in syscall_abi.h; existing calls 1–5 retain
their ABI. New errors: ENOENT=-5, EMFILE=-6, EIO=-7, EAGAIN=-8, EEXEC=-9.
Descriptors 0/1/2 do not accept file READ/CLOSE/SEEK. All syscalls preserve IF=0
through their bounded work and return through the established IRETQ path.

Host tests use real parsers/loader/dispatch with explicit VM or hardware models.
The headless filesystem suite observes an actual ELF entry through GDB, tests
filesystem behavior through the PS/2 shell, executes hostile-pointer programs and
compares process/PMM/heap accounting across repeated lifetimes. These are separate
from manual visual, physical keyboard, UEFI and real hardware acceptance.

## v0.7 disk probe

/bin/diskread is a seventh native ELF. With the supplied FAT32 fixture mounted,
it checks every byte of the fragmented file through OPEN/READ/FSTAT, EOF, seek
and failed user-copy rollback. FAT files remain readonly and non-executable.
