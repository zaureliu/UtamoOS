# v0.6 engineering review

Completed local v0.6 review; the full gate and promotion are recorded in
astra-campaign-state.md. No earlier GREEN milestone was rebuilt as new work.

## Reviewed ownership and failure paths

- Boot module: exact label and bounded count/size; immutable reserved bytes outlive
  every VFS node and descriptor. No module pages enter process ownership.
- newc: bounded header/name/data arithmetic, required trailer, duplicate/parent
  checks, canonical paths and all-or-nothing publication of the root index.
- ELF: explicit unaligned-safe reads, overflow/bounds checks, disjoint page
  extents, executable entry, W^X, zeroed BSS/stack and a bounded VM ledger.
- Loader: every post-create failure destroys the private address space; the
  caller then frees its unpublished PCB. Scheduler failure receives the same
  cleanup. Host injection covers each loader operation and publication failure.
- Process: PID is committed only after successful publication. Active VM/stack
  teardown remains deferred to another thread; shared higher-half mappings are
  never released by a child.
- Descriptors: immutable nodes, private offsets, no per-open heap allocation;
  failed user copies leave READ offset unchanged. Exit frees the owning table
  with the PCB. No kernel pointer or uninitialized output bytes are returned.
- WAIT: direct-child ownership, no consumption on EFAULT, one successful
  collection, and a separate status buffer so a negative application status
  cannot be mistaken for a pending syscall.
- ABI: established INT128/TSS/IRETQ path; no nested blocking kernel API from
  a syscall, no new FPU/TLS state, bounded IF=0 work and unchanged calls 1–5.
- Build: existing cross tools and Limine; no host libc in native programs;
  separate LOAD permissions and real BSS confirmed independently.

## Evidence available before promotion

The initial VFS/ELF headless suite passed 307 assertions. It observed the entry
of /bin/hello at CPL3 with CS=0x1b, SS=0x23, RIP=0x400000, RSP=0x6ffffef0 and a
private CR3. Three stress runs created/reaped 105 processes with stable PMM/heap
accounting. The normal boot ran init as PID 1 and three real child ELF programs.

Four host fixtures passed with AddressSanitizer and UBSan: parser/path tests
(119 assertions), per-operation loader rollback (77), native file syscalls (47)
and the real process registry/publication/WAIT lifecycle (132). Sanitized runs
are supplemental evidence, counted separately from the clean host suite.
New malformed-input tests aggregate exhaustive truncation loops and deterministic
hostile input instead of inflating the assertion count.

## Explicit limits

Read-only root, 128 nodes, 127-byte paths, immutable module up to 4 MiB.
Static native ELF only, at most 16 program headers, 1 MiB file and 128 private
pages including 16 stack pages. No writable RAMFS, symlinks, devices, persistence,
dynamic linker, POSIX execve/fork, user shell or input syscall.

SPAWN can increase interrupt latency while IF=0. WAIT polls and uses a 64-entry
result history; old uncollected results can expire. The kernel recovery exec
wait has a deadline; a timed-out application may remain scheduled. Boot init is
a controlled demonstration and exits. No real-time, SMP, FPU/TLS or global HHDM
W^X guarantee is added. Visual, UEFI and physical hardware checks remain separate.
