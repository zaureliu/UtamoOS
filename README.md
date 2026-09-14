# UTAMO OS

UTAMO OS is an experimental x86_64 operating system built from scratch in C17
and NASM Assembly for learning and exploring low-level operating-system development.

![Version v0.1.0](https://img.shields.io/badge/version-v0.1.0-blue)
[![License MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
![Architecture x86_64](https://img.shields.io/badge/architecture-x86__64-lightgrey)

| | |
| --- | --- |
| Current version | **v0.1.0** |
| Architecture | x86_64 |
| Kernel | Freestanding C17 + NASM Assembly |
| Bootloader | Limine v8.7.0 |
| Emulator | QEMU |
| Status | Experimental / early development |

## Overview

UTAMO OS has its own kernel, boot entry, terminal, drivers and small
freestanding library. It is not a Linux distribution and does not use the
Linux kernel. Linux or WSL2 provides the development environment.

The project explores real OSDev foundations incrementally: booting an ELF64
kernel, handling CPU exceptions and hardware interrupts, and accepting keyboard
input in a small kernel shell. The validated v0.0.1 baseline remains preserved
in Git; v0.1.0 builds on that history.

## Current Features

- Limine boot integration and a higher-half x86_64 kernel.
- Bitmap framebuffer terminal and COM1 serial logging.
- Validated boot memory map with usable/reclaimable memory totals.
- Own GDT, 64-bit TSS and IST stacks for critical exceptions.
- IDT with 256 entries and Assembly interrupt stubs.
- CPU exception handling with register context and serial-first diagnostics.
- Page fault diagnostics: CR2 and error-code decoding.
- Legacy PIC 8259 remapping, interrupt masks and EOI handling.
- PIT timer at a nominal 100 Hz with monotonic ticks.
- PS/2 keyboard, scancode decoding and buffered input.
- Interactive kernel shell and an interrupt-driven idle loop.

Validation combines host tests, ELF/ABI inspection, headless QEMU and the
maintainer's manual QEMU/VNC acceptance. Individual Double Fault, NMI and Machine
Check delivery paths have not been deliberately triggered; their TSS/IST
configuration is implemented and its layout is tested.

## Shell

The prompt is `utamo>`. This is a shell inside the kernel; it does not run
userspace programs.

| Command | Action |
| --- | --- |
| `help` | List implemented commands |
| `clear` | Clear the framebuffer terminal and send clear/home to the serial terminal |
| `version` | Print UTAMO OS 0.1.0 |
| `sysinfo` | Show known boot, memory, framebuffer and timer information |
| `mem` | Show boot memory-map totals, not allocator statistics |
| `uptime` | Show elapsed time estimated from PIT ticks |
| `echo text` | Print the supplied text |
| `halt` | Disable interrupts and stop the CPU |
| `fault ud2` | Trigger an Invalid Opcode exception |
| `fault div0` | Trigger a Divide Error exception |
| `fault pf` | Trigger the controlled Page Fault probe |

`fault` commands are fatal diagnostic tests: restart the VM afterward.
They never run automatically during normal boot. Input uses ASCII US scancodes;
line length is bounded and there is no command history, quoting or piping.

## Current Boot

Recorded serial output from the v0.1.0 QEMU validation
(256 MiB VM, 1024×768 framebuffer):

```text
UTAMO OS
Experimental x86_64 Operating System

Version: 0.1.0
Architecture: x86_64

[ OK    ] Limine boot protocol (base revision 3)
[ OK    ] Kernel loaded: utamo-kernel
[ OK    ] Framebuffer detected: 1024x768, 32 bpp
[ OK    ] Terminal initialized
[ OK    ] Serial COM1 initialized (115200 8N1)
[ INFO  ] Memory map entries: 18
Total usable memory: 254 MiB
[ OK    ] GDT initialized
[ OK    ] IDT initialized
[ OK    ] CPU exception handlers initialized
[ OK    ] PIC initialized
[ OK    ] PIT timer initialized (100 Hz)
[ OK    ] PS/2 keyboard initialized
[ OK    ] Interrupts enabled

UTAMO OS ready.

utamo>
```

Memory totals and framebuffer dimensions depend on the boot environment.
No screenshot is included yet; a real capture can be added after review.

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
  +-- Memory map
  +-- Kernel shell
```

The current kernel runs on one CPU in ring 0. Interrupt handlers perform short
hardware operations; input decoding and shell processing run in the main loop.
See the [architecture](docs/architecture.md) and
[interrupt frame documentation](docs/interrupts.md).

## Project Structure

```text
kernel/
  arch/x86_64/   Boot adapter, CPU/port I/O, GDT, IDT, PIC, serial, stubs
  core/         Initialization, logging, panic and shell
  drivers/      Video, PIT timer and PS/2 keyboard
  input/        Input ring buffer and scancode decoder
  interrupts/   Exception diagnostics and IRQ dispatch
  lib/          Freestanding helpers and shell parser
  memory/       Boot memory-map representation
  include/      Internal kernel interfaces
docs/           Architecture, development and release records
tests/          Host tests and validation guides
scripts/        ISO creation, ELF checks and headless QEMU harness
third_party/    Limine protocol header and provenance
.github/        Issue and pull-request templates
```

Some directories reserve space for future subsystems; their presence does not
imply an implemented filesystem, scheduler or userspace.

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
`build/utamo-os-0.1.0.iso`. `make clean` removes `build/`, including validation
logs; archive any evidence you want to keep before cleaning.

## Running

For a bounded headless boot with serial output, after building the ISO:

```sh
timeout --signal=TERM --kill-after=2s 30s \
  qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
  -cdrom build/utamo-os-0.1.0.iso -boot d -display none \
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
feed this PS/2 shell. Interactive keyboard acceptance was performed manually
through QEMU/VNC. See [debugging](docs/debugging.md) for hardware inspection
and controlled exception tests.

## Testing

The following results were recorded during v0.1.0 validation:

| Validation | Checks | Failures |
| --- | ---: | ---: |
| Host tests | 5,214 | 0 |
| ELF / ABI inspection | 1,303 | 0 |
| QEMU headless | 126 | 0 |
| **Total** | **6,643** | **0** |

These are recorded results, not a guarantee for future builds or other
hardware. The maintainer separately confirmed manual QEMU/VNC tests of PS/2
input, character typing, Enter, Backspace, shell commands, `clear` and `halt`.
Those manual checks are not added to the automated count.

See the [test guide](tests/README.md),
[implementation report](docs/v0.1-implementation-report.md) and
[validation record](docs/validation-v0.1.json).

## Roadmap

Future milestones describe planned work, not implemented features.

| Version | Milestone |
| --- | --- |
| v0.0.1 | Initial boot — validated baseline |
| v0.1.0 | Interrupts, keyboard and kernel shell |
| v0.2.0 | Physical and virtual memory management |
| v0.3.0 | Kernel heap |
| v0.4.0 | Threads and scheduler |
| v0.5.0 | Ring 3, processes and syscalls |
| v0.6.0 | VFS and userspace |
| v0.7.0 | PCI and storage |
| v0.8.0 | Networking |
| v0.9.0 | Graphics / window system |
| v1.0.0 | Stabilized experimental baseline |

The [detailed roadmap](docs/roadmap.md) records dependencies and intermediate steps.

## Documentation

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

There is no complete physical memory manager, own virtual memory manager,
kernel heap, multitasking, processes, userspace, filesystem, networking or
GUI/window manager yet. The framebuffer terminal is a text console, and the
current shell runs in the kernel.
