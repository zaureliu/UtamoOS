# Changelog

## v0.3.0 (unreleased)

GREEN local milestone on `astra-campaign`, recorded on 2026-09-14 at code
commit `2fb66be740ed8c153b3d733fc1d16bc978f2364f`. No release tag, merge into
`main` or push was performed. Public v0.1.0 and the v0.2 history are preserved.

### Added

- Kernel heap with 16-byte payload alignment, 48-byte headers, an address-ordered
  first-fit free list, block splitting, adjacent coalescing and O(N) validation.
- `kmalloc`, checked `kfree`, overflow-checked `kcalloc` and content-preserving
  `krealloc`; explicit zero-size behavior, accounting and debug payload patterns.
- Dedicated virtual heap at `0xffffc00001000000`, initially 64 KiB, growing
  in 64 KiB units up to 64 MiB through the existing PMM/HHDM/VMM.
- Supervisor writable 4 KiB backing pages with NX when available; failed
  growth rolls back newly acquired data pages and preserves the existing prefix.
- `heap` diagnostics and explicit `heaptest`, using 128 slots, seed
  `0x41535452` and 8,192 deterministic random operations per complete run.
- Host models for fragmentation, corruption, allocation failure and rollback;
  a reusable headless heap harness with PMM/VMM and shell regression checks.

### Changed

- Boot initializes the heap after PMM/VMM and before PIC/drivers/STI.
- Kernel version and derived ISO name are 0.3.0 after the heap gate passed.
- Strict warnings and existing boot, interrupt, input and memory contracts remain.

### Validation

Clean build, host tests, cross kernel, ELF/ABI inspection and ISO generation
passed. Final results: **18,994 host, 1,482 ELF/ABI and 3,203 headless QEMU
checks; 23,679 checks, zero failures**. All 14 sequential VMs were reaped.

Heap suites at 64/256/512 MiB and 64 MiB without NX each completed three
stress runs: **98,304 kernel stress operations**. Memory/shell and controlled
exception regressions passed. Counts are recorded assertions across runs,
not unique tests or a coverage measurement. See the
[validation record](docs/validation-astra-v0.3.json) and [heap guide](docs/heap.md).

### Known limits

- Single CPU and local IF exclusion; heap use from IRQ/NMI is prohibited.
- Free/realloc retain mapped pages; no decommit or arena teardown.
- Empty VMM tables may remain pinned after growth rollback.
- Metadata checks and poison patterns do not prevent stale-pointer reuse,
  out-of-bounds writes or use-after-free; HHDM alias hardening remains unchanged.
- No scheduler, userspace or SMP; allocation work can increase IRQ latency.
- Visual/physical keyboard review, UEFI and physical hardware remain manual.

## v0.2.0 (unreleased)

Implemented locally on `v0.2-dev`; awaiting maintainer acceptance. No release
tag, merge into `main` or push was performed for this milestone.

### Added

- Physical page-frame allocator for 4 KiB frames, contiguous next-fit allocation,
  checked free, double-free rejection and explicit accounting.
- Dynamically placed eligibility/occupancy bitmaps, permanent metadata/table
  reservations and a USABLE-only allocation policy.
- Validated Limine HHDM/executable-address requests and checked physical/virtual
  translation; bootloader and ACPI memory remain reserved.
- Four-level VMM on the inherited CR3: map, unmap, query and protect, with
  PMM-owned table pages, failure rollback and per-page TLB invalidation.
- Canonical-address, alignment, physical-width and page-entry validation.
  Existing 2 MiB/1 GiB mappings are detected and preserved.
- CPUID/NXE handling, CR0.WP enforcement and primary kernel-section permissions.
- Allocation-free VMM context in the existing page-fault report.
- Memory diagnostics: `mem`, `pmm`, `vmm`, `mapinfo`, `pmmtest`, `vmmtest`
  and `fault vmm`; controlled read-only/NX probes for the debug harness.
- Host fixtures, expanded ELF checks and a bounded headless memory test matrix.

### Changed

- Memory initialization runs after GDT/IDT and before PIC/drivers/STI.
- Existing architecture, shell commands, interrupt ABI and strict warnings remain.
- Version is 0.2.0 in the central kernel header; ISO naming remains derived.
- The existing QEMU shell suite reads the expected kernel version instead of
  hardcoding 0.1.0.
- Memory contracts, debugging, roadmap and implementation evidence are documented.

### Validation

Baseline v0.1.0 passed 5,214 host and 1,303 ELF checks, kernel build and ISO
generation before implementation. Final v0.2.0 results: **11,224 host,
1,439 ELF/ABI and 1,550 headless QEMU checks; 14,213 checks, zero failures**.

The 13 sequential QEMU runs used the same final ISO/ELF. They include memory
suites with 64/256/512 MiB, a 64 MiB CPU without NX, controlled page faults,
the original shell suite and exception regressions. Every VM was reaped.
These are assertions across recorded runs, not a unique-test or coverage count.

### Known limits

- Single CPU, four-level paging; no heap, scheduler, userspace or other v0.3+ work.
- Empty intermediate tables are retained; unmap does not free data frames.
- No reclaim, page-table teardown or huge-page splitting.
- Primary section permissions do not harden writable/executable HHDM aliases.
- Maintainer acceptance of v0.2.0, visual framebuffer/physical keyboard review,
  UEFI and physical hardware validation remain pending.

See the [implementation report](docs/v0.2-implementation-report.md),
[validation record](docs/validation-v0.2.json) and
[memory-management guide](docs/memory-management.md).

## v0.1.0

Released milestone: interrupts, keyboard and an interactive kernel shell.
Validation and manual acceptance were recorded on 2026-09-14.

### Added

- Own x86_64 GDT, 64-bit TSS and IST stacks for critical exceptions.
- A complete 256-entry IDT, NASM interrupt stubs and a normalized 176-byte frame.
- Fatal CPU exception diagnostics with registers, serial-first output and framebuffer output.
- Page fault diagnostics with CR2 and decoded error bits.
- PIC 8259 remapping, masks, EOI and spurious IRQ handling.
- PIT channel 0 at a nominal 100 Hz and monotonic ticks.
- PS/2 set 1 keyboard, circular input buffer and independent scancode decoder.
- Interactive shell: `help`, `clear`, `version`, `sysinfo`, `mem`,
  `uptime`, `echo`, `halt`, and `fault ud2|div0|pf`.
- Host tests, ELF/ABI inspection and bounded headless QEMU validation.
- Public contribution/security guidance, GitHub issue/PR templates and release notes.

### Changed

- Normal boot reaches `utamo>` and idles with STI/HLT.
- Existing boot, framebuffer, serial, memory-map and freestanding interfaces preserved.
- Version centralized in the kernel header; ISO name derived from that version.
- Strict C and NASM warnings retained, including the validated narrow NASM
  `reloc-rel-dword` exception to `-Werror`.
- Architecture, debugging, build and public documentation updated.

### Validation

All 3,044 previous host checks were preserved. Recorded totals: **5,214 host,
1,303 ELF/ABI and 126 headless QEMU checks; 6,643 checks, zero failures**.
The kernel and ISO were built with the existing cross toolchain.

The maintainer separately confirmed manual QEMU/VNC acceptance of PS/2 input,
typing, Enter, Backspace, shell commands, `clear` and `halt`.
See the [release notes](docs/releases/v0.1.0.md) and
[implementation report](docs/v0.1-implementation-report.md) for evidence and limits.

## UTAMO OS 0.0.1 — 2026-09-13

Registro histórico da geração inicial, antes do primeiro boot validado.
O baseline v0.0.1 foi posteriormente compilado e validado em QEMU, como
registrado no development log; as notas abaixo preservam o estado original.

### Added

- Initial x86_64 kernel (`utamo-kernel`), entrada NASM, pilha de 64 KiB e ELF64 higher half.
- Limine boot integration: release v8.7.0, base revision 3, API revision 2.
- Framebuffer abstraction com RGB 24/32 bpp e validação de geometria/máscaras.
- Terminal próprio com cursor lógico, newline, wrap e fonte ASCII incorporada.
- Logging com níveis INFO, OK, WARN, ERROR, DEBUG e múltiplos destinos.
- Serial support: COM1, 115200 8N1, loopback e polling limitado.
- Panic handler com arquivo/linha, prevenção de reentrada e halt permanente.
- Basic memory map parsing, cópia própria, validação de intervalos e soma utilizável.
- Funções mínimas de memória/strings e formatter com saída por callback ou buffer limitado.
- Build GNU Make, preparação local de ISO BIOS/UEFI, targets QEMU/GDB e inspeção ELF.
- Testes de host preparados, matriz de validação de boot e documentação técnica.

### Validation

- Duas revisões estáticas dos fontes e da integração, com correções registradas.
- Compiladores, testes, scripts, ISO e QEMU não executados nesta geração.
- Não há resultados de boot ou de testes automatizados a relatar.
