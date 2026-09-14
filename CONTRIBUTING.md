# Contributing to UTAMO OS

UTAMO OS is an experimental x86_64 kernel. Contributions should preserve its freestanding architecture and keep implemented behavior distinct from future plans.

## Making changes

- Work on a branch and submit a pull request with small, reviewable changes.
- Describe the problem, resulting behavior, and validation performed.
- Follow the [coding style](docs/coding-style.md) and existing interfaces. Document architectural changes in the relevant [technical documentation](docs/architecture.md).
- Keep all configured warnings enabled: warnings are errors. The specific NASM `reloc-rel-dword` exception in the Makefile is intentional.
- Do not introduce libc, Linux APIs, Linux syscalls, or other host dependencies into the kernel. Host test programs may use the host runtime.
- Keep hardware access separate from logic that can be tested on the host. Do not execute privileged instructions in host tests.
- Keep generated binaries, local toolchains, and Limine vendor assets out of commits.

## Building and testing

Use Linux or WSL2 Ubuntu with the existing x86_64-elf cross toolchain. See the [development environment](docs/development-environment.md) for requirements and Limine setup. The native compiler is for host tests; the kernel requires the cross compiler.

Run these commands from the repository root, with the toolchain at the documented project-local path:

```sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
```

For changes affecting boot or hardware, perform the relevant QEMU validation and record the result. A bounded headless boot check is available:

```sh
python3 scripts/test-qemu.py --marker "utamo> " --name contribution-boot \
    --check-gdt --check-idt --check-timer
```

Use a fresh validation name to preserve previous evidence. Run one QEMU instance at a time and ensure it exits. Automated runs must use headless output; GTK/SDL can cause WSLg problems. See [debugging](docs/debugging.md) and the [test guide](tests/README.md) for exception probes and validation limits.

Report commands actually run and identify anything still requiring manual validation. Host tests and ELF inspection do not establish keyboard, framebuffer, or hardware behavior by themselves.

## Before opening a pull request

Review `git diff --check` and `git status`. Include the relevant tests and documentation, keep unrelated changes separate, and explain any boot behavior that changed.
