# UTAMO OS

UTAMO OS is an experimental x86_64 operating system built from scratch in C17
and NASM Assembly for learning and exploring low-level operating-system development.

![Development v0.4.0](https://img.shields.io/badge/development-v0.4.0-blue)
[![License MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
![Architecture x86_64](https://img.shields.io/badge/architecture-x86__64-lightgrey)

| | |
| --- | --- |
| Development version | **v0.4.0** on `astra-campaign` |
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
support **v0.4.0 kernel threads and preemptive scheduling**, whose local Astra
campaign gate is GREEN. The campaign has not been tagged, merged into main or
published; both public baseline tags remain unchanged. v0.5 Ring 3 work is planning.

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

New mappings use 4 KiB pages. Existing 2 MiB/1 GiB leaves are queried and
preserved; they are never silently split. The public mapping API modifies only its
reserved virtual arena. HHDM aliases retain bootloader permissions, so section
protections do **not** establish global W^X.

See [memory management](docs/memory-management.md) and [the heap guide](docs/heap.md)
for ownership rules, CPU requirements and limits. Individual Double Fault, NMI and Machine Check
delivery paths have not been deliberately triggered.

## Shell

The prompt is `utamo>`. Commands execute inside the kernel; there are no
userspace programs.

| Command | Action |
| --- | --- |
| `help` | List implemented commands |
| `clear` | Clear the framebuffer terminal and send clear/home to serial |
| `version` | Print UTAMO OS 0.4.0 |
| `sysinfo` | Show known boot, memory, framebuffer and timer information |
| `mem` | Show boot-map totals, PMM accounting and VMM configuration |
| `pmm` | Show managed/used/free frames and bitmap storage |
| `vmm` | Show CR3 root, HHDM, address width, NX and table accounting |
| `mapinfo <hex-address>` | Query a canonical address without changing page tables |
| `pmmtest` | Run bounded allocation, marker, free and accounting checks |
| `vmmtest` | Exercise mapping, permissions, partial unmap and table reuse |
| `heap` | Show heap capacity, allocation accounting and integrity |
| `heaptest` | Run bounded deterministic allocation/reallocation stress |
| `ps` / `threads` | List real kernel-thread snapshots |
| `schedulerstats` | Show scheduler counters, queues and integrity |
| `schedtest` | Exercise preemption, registers, sleep and thread lifecycle |
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

Recorded serial output from the final v0.4.0 headless scheduler suite
(256 MiB VM, 1024x768 framebuffer reported by Limine), preserved in
`build/validation/astra-v04-final-scheduler/serial.log`:

```text
UTAMO OS
Experimental x86_64 Operating System

Version: 0.4.0
Architecture: x86_64

[ OK    ] Limine boot protocol (base revision 3)
[ OK    ] Kernel loaded: utamo-kernel
[ OK    ] Framebuffer detected: 1024x768, 32 bpp
[ OK    ] Terminal initialized
[ OK    ] Serial COM1 initialized (115200 8N1)
[ INFO  ] Memory map entries: 16
Total usable memory: 253 MiB
[ OK    ] GDT initialized
[ OK    ] IDT initialized
[ OK    ] CPU exception handlers initialized
[ OK    ] PMM initialized
[ INFO  ] Physical frames: 64975
[ INFO  ] Free frames: 64971
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
[ OK    ] PS/2 keyboard initialized
[ OK    ] Interrupts enabled

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
  +-- Kernel shell / idle thread
```

The current kernel runs on one CPU in ring 0. Interrupt handlers perform short
hardware operations; input decoding and shell processing run in the bootstrap
thread, which blocks for keyboard input. Idle runs when no other thread is ready.
See [threads and scheduling](docs/scheduler.md), [memory management](docs/memory-management.md), the
[architecture](docs/architecture.md) and
[interrupt frame documentation](docs/interrupts.md).

## Project Structure

```text
kernel/
  arch/x86_64/   Boot adapter, CPU/port I/O, GDT, IDT, PIC, serial, stubs
  core/         Initialization, logging, panic, shell, threads and scheduler
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
imply an implemented filesystem or userspace.

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
`build/utamo-os-0.4.0.iso`. `make clean` removes `build/`, including validation
logs; archive any evidence you want to keep before cleaning.

## Running

For a bounded headless boot with serial output, after building the ISO:

```sh
timeout --signal=TERM --kill-after=2s 30s \
  qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
  -cdrom build/utamo-os-0.4.0.iso -boot d -display none \
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
```

This also exercises shell commands, selftests, editing and halt. See
[debugging](docs/debugging.md) for controlled faults and the NX-disabled case.
No window opens and no framebuffer capture is required.

## Testing

The v0.4.0 gate is recorded in [machine-readable evidence](docs/validation-astra-v0.4.json)
and [campaign state](docs/astra-campaign-state.md). Its local summary is
`validation-artifacts/astra-v04-final-20260914T084853Z/summary.json`; frozen
ELF, ISO and evidence are in `validation-artifacts/astra-last-known-good/v0.4.0/`.

| Validation phase | Checks | Failures |
| --- | ---: | ---: |
| Final host tests | 22,794 | 0 |
| Final ELF / ABI inspection | 1,563 | 0 |
| Candidate QEMU matrix, banner 0.3.0, 15 VMs | 6,081 | 0 |
| Final 0.4.0 boot/scheduler/stack-fault checks, 3 VMs | 1,243 | 0 |
| **Total recorded** | **31,681** | **0** |

The candidate matrix covered scheduler suites at 64/256/512 MiB and without NX,
allocator/shell regressions and exceptions. After changing the version stamp,
the final three VMs checked boot, the scheduler suite and the guard-page fault.
All 18 sequential BIOS QEMU/TCG VMs passed and were reaped. Binary comparison
found identical `.text`, `.data` and `.limine_requests`, with one version byte
different in `.rodata`; the complete RAM/NX matrix was not repeated after stamping.

Counts sum final host/ELF once and both QEMU phases. They describe assertions,
not unique tests or coverage. The 14 existing v0.3 host suites remain present;
only the new stack fixtures aggregate repeated observations per scenario.
Historical [v0.3](docs/validation-astra-v0.3.json), [v0.2](docs/validation-v0.2.json)
and [v0.1](docs/validation-v0.1.json) records remain separate.

Serial, QMP PS/2 input and GDB provide the automated evidence. Physical keyboard
interaction, visual framebuffer review, UEFI and physical hardware acceptance
for v0.4 remain manual. Earlier QEMU/VNC acceptance applies to v0.1.0.

See the [test guide](tests/README.md) for coverage and reproduction commands.

## Roadmap

Future milestones describe planned work, not implemented features.

| Version | Milestone |
| --- | --- |
| v0.0.1 | Initial boot — validated baseline |
| v0.1.0 | Interrupts, keyboard and kernel shell - public baseline |
| v0.2.0 | Physical and virtual memory management - implemented locally, awaiting acceptance |
| v0.3.0 | Kernel heap - GREEN local campaign gate |
| v0.4.0 | Threads and scheduler - GREEN local campaign gate |
| v0.5.0 | Ring 3, processes and syscalls - planning |
| v0.6.0 | VFS and userspace |
| v0.7.0 | PCI and storage |
| v0.8.0 | Networking |
| v0.9.0 | Graphics / window system |
| v1.0.0 | Stabilized experimental baseline |

The [detailed roadmap](docs/roadmap.md) records dependencies and intermediate steps.

## Documentation

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

PMM, the initial VMM, heap and preemptive kernel threads are implemented.
Threads share one address space and use general registers only. There are no
isolated processes, userspace, filesystem, networking or GUI/window manager.
The heap retains mapped pages, grows from 64 KiB up to 64 MiB and uses local
interrupt exclusion; it must not be called from IRQ/NMI context. The framebuffer
terminal is a text console, and the current shell runs in the kernel. There is
no SMP memory synchronization, page-table reclamation, bootloader-memory reclaim
or global HHDM alias hardening yet.
