# Native ELF loading and process construction

GREEN local v0.6.0; [gate evidence](validation-astra-v0.6.json). The loader consumes immutable VFS file bytes. The pure parser
uses explicit little-endian reads rather than casting possibly unaligned input.
The [ELF program-header specification](https://gabi.xinuos.com/elf/07-pheader.html)
defines the file layout; UTAMO accepts static ET_EXEC, ELF64, x86_64 only.

Validation checks header/class/version/endianness/OS ABI, header-table bounds,
file and virtual extents, page and p_align congruence, power-of-two alignment,
overflow, user canonical range, stack/guard exclusion, resource limits and
disjoint page ranges. At most 16 headers and 1 MiB of file bytes are accepted.
PT_DYNAMIC, PT_INTERP and unknown required segment types are rejected; metadata
NULL/NOTE/PHDR and a non-executable GNU_STACK do not create mappings.
Every LOAD requires R and nonzero memsz; W+X is rejected. Entry must be inside
file-backed executable bytes. The native linker emits distinct RX, R and RW
LOADs; BSS remains zero-filled.

The loader creates an inactive private VM with the v0.5 ownership ledger.
Image pages begin zeroed RW/NX, receive file bytes, then receive final permissions
before thread publication. It allocates 16 zeroed stack pages below 0x70000000;
the page below them remains absent. Image plus stack must fit 128 user pages.
Every failed allocation/copy/protection destroys the unpublished VM and returns
all of its private pages/tables. Output entry/stack/argument values remain unchanged.

Native entry: RIP from ELF, RSP=0x6ffffef0 (16-byte aligned), RDI=0x6fffff00,
pointing to one NUL-terminated argument string (maximum 255 bytes).
Assembly _start calls user_main, then EXIT; no host CRT, Linux ABI, environment
variables, dynamic linking, TLS, FPU/SIMD state or POSIX argv layout is implied.

SPAWN creates a fresh child and leaves the caller's image running. Kernel `exec`
uses the same path to create and await a process; it is not POSIX execve.
Construction is bounded and serialized on the BSP with IF=0, including from
native syscalls. This preserves publication/VM ownership but increases interrupt
latency; there is no real-time claim or allocator use from IRQ/NMI handlers.

PID 1 is /bin/init. At normal NX-enabled boot it launches and waits for hello,
echo and sysinfo, then exits after a controlled startup demonstration. Without
NX, the filesystem mounts and kernel shell remains available; ELF creation is
refused. The boot wait is bounded. User process faults retain v0.5 containment.

WAIT accepts only a direct child. While it is active or pending deferred reap,
the result is EAGAIN. After reaping, it copies the full signed 64-bit exit status
to a checked user buffer and consumes the result only after a successful copy.
Thus any application's exit code is distinct from syscall errors. Results use the
existing bounded 64-entry history; old uncollected results can expire when it wraps.
No blocking wait queues, process reparenting, fork or image replacement are provided.
