# UTAMO OS

UTAMO OS is an experimental x86_64 operating system built from scratch in C17
and NASM Assembly for learning and exploring low-level operating-system development.

![Development v0.6.0](https://img.shields.io/badge/development-v0.6.0-blue)
[![License MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
![Architecture x86_64](https://img.shields.io/badge/architecture-x86__64-lightgrey)

| | |
| --- | --- |
| Development version | **v0.6.0** on `astra-campaign` |
| Latest public release | **v0.1.0** |
| Architecture | x86_64 |
| Kernel | Freestanding C17 + NASM Assembly |
| Bootloader | Limine v8.7.0 |
| Emulator | QEMU |
| Status | Experimental / early development |

## Overview

UTAMO OS has its own kernel, boot entry, terminal, drivers and small
freestanding library. It is not a Linux distribution and does not use the
Linux kernel. Linux or WSL2 provides the development environment.

Development proceeds through preserved milestones. The public baseline is
[v0.1.0](docs/releases/v0.1.0.md). The preserved memory and heap milestones now
support **v0.6.0 VFS, initramfs and native ELF userspace**, whose local Astra
campaign gate is GREEN. The campaign has not been tagged, merged into main or
published; both public baseline tags remain unchanged. v0.7 PCI/storage follows this preserved checkpoint.

## Current Features

- Limine boot integration and a higher-half x86_64 kernel.
- Bitmap framebuffer terminal and COM1 serial logging.
- Validated boot memory map, explicit HHDM translation and reserved-memory policy.
- Own GDT, 64-bit TSS and IST stacks; 256-entry IDT and NASM interrupt stubs.
- CPU exception context and serial-first page fault diagnostics with a VMM query.
- Legacy PIC 8259, PIT at nominal 100 Hz and an interrupt-driven idle loop.
- PS/2 keyboard, scancode decoding, buffered input and a kernel shell.
- PMM with 4 KiB frames, dynamically placed eligibility/occupancy bitmaps,
  contiguous allocation, checked free, accounting and permanent reservations.
- Four-level VMM using the inherited CR3: map, unmap, query and protect;
  PMM-owned intermediate tables, rollback on allocation failure and `invlpg`.
- CPUID-based NX detection, NXE handling and primary kernel-section protections.
- Controlled PMM/VMM stress tests and unmap, read-only and NX fault probes.
- Kernel heap with 16-byte alignment, split/coalesce, checked free, calloc/realloc,
  PMM/VMM-backed growth, rollback, accounting and deterministic stress tests.
- Kernel threads, two-tick round-robin preemption, sleep/wakeup and deferred reaping.
- Guarded 64 KiB thread stacks, register/flag preservation probes and scheduler diagnostics.
- Private user address spaces, TSS CPL3 entry, user-page W^X and contained faults.
- Native INT128 WRITE/EXIT/GETPID/YIELD/SLEEP with bounded checked user copies.
- Embedded user probes, 16-process capacity rejection and process diagnostics.
- Immutable newc initramfs, VFS, private file descriptors and checked file syscalls.
- Strict ELF64 loader, zeroed BSS/stacks, native runtime and PID 1 init.
- Separate hello, echo and sysinfo programs, plus VFS/hostile-buffer tests.

New mappings use 4 KiB pages. Existing 2 MiB/1 GiB leaves are queried and
preserved; they are never silently split. The public mapping API modifies only its
reserved virtual arena. HHDM aliases retain bootloader permissions, so section
protections do **not** establish global W^X.

See [memory management](docs/memory-management.md) and [the heap guide](docs/heap.md)
for ownership rules, CPU requirements and limits. Individual Double Fault, NMI and Machine Check
delivery paths have not been deliberately triggered.

## Shell

The prompt is `utamo>`. Commands execute inside the kernel; `usertest` launches
controlled embedded CPL3 probes. Native init launches real ELF programs;
exec runs files from the immutable VFS.

| Command | Action |
| --- | --- |
| `ls [path]` / `cat path` | List/read immutable VFS content |
| `exec path [argument]` | Create and await a native ELF process |
| `fstest` | Stress VFS, ELF, bad pointers, descriptors and cleanup |
| `help` | List implemented commands |
| `clear` | Clear the framebuffer terminal and send clear/home to serial |
| `version` | Print UTAMO OS 0.6.0 |
| `sysinfo` | Show known boot, memory, framebuffer and timer information |
| `mem` | Show boot-map totals, PMM accounting and VMM configuration |
| `pmm` | Show managed/used/free frames and bitmap storage |
| `vmm` | Show CR3 root, HHDM, address width, NX and table accounting |
| `mapinfo <hex-address>` | Query a canonical address without changing page tables |
| `pmmtest` | Run bounded allocation, marker, free and accounting checks |
| `vmmtest` | Exercise mapping, permissions, partial unmap and table reuse |
| `heap` | Show heap capacity, allocation accounting and integrity |
| `heaptest` | Run bounded deterministic allocation/reallocation stress |
| `ps` / `threads` | List real scheduled-thread snapshots |
| `schedulerstats` | Show scheduler counters, queues and integrity |
| `schedtest` | Exercise preemption, registers, sleep and thread lifecycle |
| `processes` | Show process availability, lifetimes, faults, syscalls and CR3 switches |
| `usertest` | Exercise CPL3 isolation, syscalls, hostile probes, capacity and cleanup |
| `sleep <decimal-ms>` | Sleep this thread; zero yields voluntarily |
| `uptime` | Show elapsed time estimated from PIT ticks |
| `echo text` | Print the supplied text |
| `halt` | Disable interrupts and stop the CPU |
| `fault ud2` | Trigger an Invalid Opcode exception |
| `fault div0` | Trigger a Divide Error exception |
| `fault pf` | Trigger the original controlled Page Fault probe |
| `fault vmm` | Trigger a Page Fault after mapping and unmapping a test page |
| `fault stack` | Trigger a supervisor write Page Fault on the idle stack guard |

`fault` commands are fatal diagnostic tests: restart the VM afterward.
Read-only and NX probes are available through the headless debug harness, not
as arbitrary page-table editing commands. No memory selftest or fatal probe
runs automatically during normal boot.

Input uses ASCII US scancodes; line length is bounded and there is no command
history, quoting or piping. The first `vmmtest` retains its intermediate page
tables; repeated runs reuse them. Data frames are released.

## Current Boot

Recorded serial output from the final v0.6.0 headless filesystem suite
(256 MiB VM, 1024x768 framebuffer reported by Limine), preserved in
`build/validation/astra-v06-final-fs/serial.log`:

```text
[ INFO  ] Starting UTAMO kernel
==============================================
UTAMO OS
Experimental x86_64 Operating System

Version: 0.6.0
Architecture: x86_64

[ OK    ] Limine boot protocol (base revision 3)
[ OK    ] Kernel loaded: utamo-kernel
[ OK    ] Framebuffer detected: 1024x768, 32 bpp
[ OK    ] Terminal initialized
[ OK    ] Serial COM1 initialized (115200 8N1)
[ INFO  ] Memory map entries: 19
Total usable memory: 253 MiB
[ OK    ] GDT initialized
[ OK    ] IDT initialized
[ OK    ] CPU exception handlers initialized
[ OK    ] PMM initialized
[ INFO  ] Physical frames: 64877
[ INFO  ] Free frames: 64873
[ INFO  ] PMM metadata: phys=0x53000, 16384 bytes
[ OK    ] VMM initialized
[ INFO  ] HHDM offset: 0xffff800000000000
[ INFO  ] CR3 preserved: 0xff4c000; inherited table visits: 10
[ INFO  ] NX supported: Yes; enabled: Yes
[ INFO  ] Kernel section protections: applied
[ OK    ] Kernel heap initialized
[ OK    ] PIC initialized
[ OK    ] PIT timer initialized (100 Hz)
[ OK    ] Kernel scheduler initialized (round-robin, 2 ticks)
[ OK    ] Ring 3 process infrastructure initialized
[ OK    ] VFS initramfs mounted: 13 nodes, 138676 bytes
[ OK    ] PS/2 keyboard initialized
[ OK    ] Interrupts enabled
init: PID 1 executing native ELF programs
Hello from UTAMO userspace!
echo from an ELF process
UTAMO native userspace: x86_64; PID 4; free pages 64754; PIT ticks 4
init: controlled startup complete

UTAMO OS ready.

utamo>
```

Memory totals and addresses depend on the boot environment. This is serial
evidence; no screenshot or visual framebuffer result is implied.

## Architecture

```text
Firmware
  |
Limine
  |
UTAMO Kernel
  +-- x86_64 architecture layer
  +-- GDT / TSS / IST
  +-- IDT / CPU exceptions
  +-- PIC / PIT
  +-- PS/2 keyboard and input buffer
  +-- Framebuffer terminal / COM1
  +-- Memory map / HHDM
  +-- PMM: physical frames and reservations
  +-- VMM: page tables, permissions and TLB
  +-- Heap: allocation, growth and integrity
  +-- Kernel threads / round-robin scheduler / guarded stacks
  +-- Private user VM / CPL3 / native INT128 syscalls
  +-- Kernel shell / idle thread / native ELF init and user programs
```

The kernel runs on one CPU in ring 0; isolated processes execute at CPL3.
[Processes](docs/processes.md) and [syscalls](docs/syscalls.md) describe this boundary.
Interrupt handlers perform short
hardware operations; input decoding and shell processing run in the bootstrap
thread, which blocks for keyboard input. Idle runs when no other thread is ready.
See [threads and scheduling](docs/scheduler.md), [memory management](docs/memory-management.md), the
[architecture](docs/architecture.md) and
[interrupt frame documentation](docs/interrupts.md).

## Project Structure

```text
kernel/
  arch/x86_64/   Boot adapter, CPU/port I/O, GDT, IDT, PIC, serial, stubs
  core/         Initialization, shell, scheduler, processes and syscalls
  drivers/      Video, PIT timer and PS/2 keyboard
  input/        Input ring buffer and scancode decoder
  interrupts/   Exception diagnostics and IRQ dispatch
  lib/          Freestanding helpers and shell parser
  memory/       Memory map, HHDM, PMM, VMM, heap and bounded selftests
  include/      Internal kernel interfaces
docs/           Architecture, development and release records
tests/          Host tests and validation guides
scripts/        ISO creation, ELF checks and headless QEMU harness
third_party/    Limine protocol header and provenance
.github/        Issue and pull-request templates
```

Some directories reserve space for future subsystems; their presence does not
imply an implemented filesystem or general executable loader.

## Building

Use Linux or WSL2 Ubuntu, a project path without spaces, and the
[development environment guide](docs/development-environment.md).

Required tools: an **x86_64-elf GCC** cross compiler, GNU Binutils for that target,
NASM, GNU Make, a host C compiler, Bash, Python 3, Git, xorriso and the pinned
Limine **v8.7.0-binary** assets. QEMU and GDB are used for runtime validation.
The validated toolchain used GCC 14.2.0, Binutils 2.43.1 and NASM 3.01.

The commands below assume the cross toolchain is installed under
`toolchain/prefix/` and the pinned Limine checkout is provisioned under
`third_party/limine/vendor/`. These local directories are not committed.
The Makefile does not download or install them.

From the project root:

```sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
```

The native compiler is used only for host tests. The kernel must use the
cross compiler. Outputs are `build/utamo-kernel.elf` and
`build/utamo-os-0.6.0.iso`. `make clean` removes `build/`, including validation
logs; archive any evidence you want to keep before cleaning.

## Running

For a bounded headless boot with serial output, after building the ISO:

```sh
timeout --signal=TERM --kill-after=2s 30s \
  qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
  -cdrom build/utamo-os-0.6.0.iso -boot d -display none \
  -serial stdio -monitor none -nic none -no-reboot -no-shutdown
```

The VM stays at the shell prompt until the timeout ends it; exit code 124 is
the timeout result, not a kernel exit code. No GTK/SDL window is required.
Headless operation avoids the WSLg/RemoteApp display problems encountered in
the development environment.

To check the prompt, GDT/IDT and advancing ticks automatically:

```sh
python3 scripts/test-qemu.py --marker 'utamo> ' --name boot-local \
  --check-gdt --check-idt --check-timer
```

Use a new `--name` for each run. The harness limits runtime and reaps its VM.
Run one QEMU instance at a time.

Serial is an output console; typing into the serial host terminal does not
feed this PS/2 shell. The headless memory harness
feeds emulated PS/2 input using QMP, with serial as the primary evidence:

```sh
python3 scripts/test-memory-qemu.py --suite --name memory-local --ram 256M
python3 scripts/test-heap-qemu.py --suite --name heap-local --ram 256M --timeout 240
python3 scripts/test-scheduler-qemu.py --suite --name scheduler-local --ram 256M --timeout 300
python3 scripts/test-process-qemu.py --suite --name process-local --ram 256M --timeout 300
```

This also exercises shell commands, selftests, editing and halt. See
[debugging](docs/debugging.md) for controlled faults and the NX-disabled case.
No window opens and no framebuffer capture is required.

## Testing

The v0.6 gate is recorded in [evidence](docs/validation-astra-v0.6.json) and
[campaign state](docs/astra-campaign-state.md). Frozen artifacts:
`validation-artifacts/astra-last-known-good/v0.6.0/`.

| Phase | Checks | Failures |
| --- | ---: | ---: |
| Final clean host | 25,926 | 0 |
| Final kernel and native ELF / ABI | 1,843 | 0 |
| Candidate headless matrix, 23 passing VMs | 11,293 | 0 |
| Final 0.6.0 VFS / NX-off / RO checks, 3 VMs | 466 | 0 |
| **Passing gate total** | **39,528** | **0** |

The candidate covers 64/256/512 MiB, NX-off, VFS/ELF/process stress, allocator,
scheduler, shell and fatal probes. All 26 passing VMs were reaped. One earlier
RO probe harness timed out before init completed; readiness-based arming fixed
it. The failed attempt remains separate: 27 actual VM attempts.

Kernel text/data/requests match across stamping, with one rodata version byte
changed. Native runtime sections also match; debug paths were normalized.
The full RAM matrix was not repeated after stamping. Final host/ELF count once
plus both QEMU phases; these are assertions, not unique tests or coverage.
Supplemental AddressSanitizer/UBSan runs are separate.

Twelve VFS/ELF stress runs created/reaped 420 processes. Nine embedded-probe
runs created/reaped 360 processes and contained 117 user faults. Startup and
explicit exec checks are separate. See [the review](docs/audit-astra-v0.6.md).
Visual, physical keyboard, UEFI and real hardware checks remain separate;
earlier manual acceptance belongs to v0.1.0.

## Roadmap

Future milestones describe planned work, not implemented features.

| Version | Milestone |
| --- | --- |
| v0.0.1 | Initial boot — validated baseline |
| v0.1.0 | Interrupts, keyboard and kernel shell - public baseline |
| v0.2.0 | Physical and virtual memory management - implemented locally, awaiting acceptance |
| v0.3.0 | Kernel heap - GREEN local campaign gate |
| v0.4.0 | Threads and scheduler - GREEN local campaign gate |
| v0.5.0 | Isolated Ring 3 processes and syscalls - GREEN local campaign gate |
| v0.6.0 | VFS, initramfs, ELF and native userspace - GREEN local gate |
| v0.7.0 | PCI and storage |
| v0.8.0 | Networking |
| v0.9.0 | Graphics / window system |
| v1.0.0 | Stabilized experimental baseline |

The [detailed roadmap](docs/roadmap.md) records dependencies and intermediate steps.

## Documentation

- [VFS](docs/vfs.md), [ELF loader](docs/elf-loader.md), [userspace](docs/userspace.md) and [v0.6 evidence](docs/validation-astra-v0.6.json)

- [Processes and isolation](docs/processes.md), [syscall ABI](docs/syscalls.md) and [v0.5 validation](docs/validation-astra-v0.5.json)

- [Threads and scheduler](docs/scheduler.md), [v0.4 validation](docs/validation-astra-v0.4.json) and [Astra campaign state](docs/astra-campaign-state.md)
- [Kernel heap](docs/heap.md) and [v0.3 validation](docs/validation-astra-v0.3.json)
- [Memory management](docs/memory-management.md) and [v0.2 implementation report](docs/v0.2-implementation-report.md)
- [Architecture](docs/architecture.md)
- [Development environment](docs/development-environment.md)
- [Boot process](docs/boot-process.md) and [memory layout](docs/memory-layout.md)
- [Interrupts and exception frame](docs/interrupts.md)
- [PS/2 keyboard and shell](docs/keyboard.md)
- [Debugging](docs/debugging.md) and [tests](tests/README.md)
- [Coding style](docs/coding-style.md) and [development log](docs/development-log.md)
- [Changelog](CHANGELOG.md) and [v0.1.0 release notes](docs/releases/v0.1.0.md)

Technical documentation is currently primarily in Portuguese.
See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines and
[SECURITY.md](SECURITY.md) for the security policy.

## License

UTAMO OS is licensed under the [MIT License](LICENSE).
Third-party notices and Limine provenance are documented in
[third_party/limine](third_party/limine/README.md).

## Project Status

**UTAMO OS is experimental software and is not intended for production use.**

PMM, VMM, heap, preemptive threads, isolated processes, immutable VFS and
static native ELF userspace are implemented. User processes require NX.
The root is read-only; disk storage, networking, input syscalls, a userspace
shell, GUI, POSIX, SMP, TLS and FPU/vector context switching remain absent.

Heap pages and empty kernel page tables remain retained; process destruction
returns private user pages/tables. Modules stay reserved. SPAWN is bounded but
keeps IF=0 during construction. The kernel shell remains the recovery interface;
global HHDM alias hardening remains future work.
