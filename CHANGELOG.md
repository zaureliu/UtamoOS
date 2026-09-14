# Changelog

## v0.7.0 (unreleased)

GREEN local at 0cfca41fadfb298297bd740a347bc3dfcb022ebe; no push, merge or tag.

### Added

- Generic PCI enumeration/BAR decoding and bootstrap BAR sizing with restoration.
- Dedicated supervisor UC MMIO mappings and bounded readonly AHCI DMA.
- Bounds-checked block reads and transactional FAT32 snapshot import at /disk.
- Short-name root/subdirectories, corruption rejection and readonly cached VFS mounts.
- lspci/storage/disktest diagnostics and native Ring 3 diskread validation.

### Validation and limits

Final clean host/ELF: 30,022 / 1,916. Candidate QEMU: 12,202 in 34 VMs;
final confirmation: 403 in three VMs. Passing total 44,543, zero gate failures.
All 37 gate VMs were reaped. One preliminary pass and two historical failures
remain separate (40 total attempts). Runtime sections, seven complete user ELFs
and initramfs match across stamping; only one kernel version byte changed.

Eighteen storage stress runs performed 162 fresh imports, with exact snapshot
and allocator comparisons. Single-disk 512-byte LBA48 reads only; no storage
writes, partition discovery, hotplug or recovery. FAT32 is a bounded immutable
8.3 cache, not a full compatibility implementation. Ambiguous DMA failures
quarantine the owned pages. See [storage](docs/storage.md),
[the audit](docs/audit-astra-v0.7.md) and [evidence](docs/validation-astra-v0.7.json).

## v0.6.0 (unreleased)

GREEN local at c5b2a915422df7c6d71e353335e6fc812225ea62; no push, merge or tag.

### Added

- Bounded immutable newc initramfs and read-only VFS with private file offsets.
- Strict ELF64 loader, W^X, zeroed BSS/stacks and complete rollback.
- Native runtime and separate init (PID 1), hello, echo and sysinfo programs.
- OPEN/READ/CLOSE/SEEK/FSTAT/SPAWN/WAIT/INFO with checked buffers and ownership.
- Kernel ls/cat/exec/fstest, actual CPL3 ELF observation and lifecycle stress.

### Validation and fixes

Final clean host/ELF: 25,926 / 1,843. Candidate QEMU: 11,293 in 23 VMs;
final confirmation: 466 in three VMs. Passing total 39,528, zero gate failures,
26 passing VMs reaped. One prior harness readiness timeout remains separate;
arming the probe after init fixed it. Runtime section bytes match across the
final version/debug-path changes. Full RAM matrix ran on the candidate.

WAIT separates signed exit status from syscall errors. Failed READ/WAIT copies
preserve offsets/results. Sanitizers cover loader rollback, file calls and real
process publication/WAIT. [Evidence](docs/validation-astra-v0.6.json).

### Limits

Read-only root; static native ELF; no input syscall, user shell, POSIX execve,
disk or network. SPAWN keeps IF=0; WAIT uses finite history. Manual/UEFI/hardware
validation is separate.

## v0.5.0 (unreleased)

GREEN local milestone on `astra-campaign`, recorded on 2026-09-14 at
`4f303c8aeca26051f4affe92b0ba5af4e2296026`. No public tag, merge or push.

### Added

- Up to 16 single-threaded CPL3 processes with private PML4s, exclusive user
  frames, checked copies and complete private address-space teardown.
- User GDT selectors, TSS.RSP0 switching, guarded user/kernel stacks and
  NX-required user W^X; shared higher-half mappings remain supervisor.
- Native INT128 WRITE/EXIT/GETPID/YIELD/SLEEP, bounded buffers and explicit errors.
- Serial-first contained user exceptions; kernel/critical IST faults remain fatal.
- `processes`/`usertest`, embedded probes, capacity rejection and a headless
  process harness with actual CPL3 observation, plus host models.

### Changed and fixed

- Scheduler selects kernel/private CR3 and reaps user resources from another thread.
- Invalid user return state preserves IRQ accounting/wakeup before termination.
- User fault queries report effective higher-half permissions; self-test waits
  reject deadline overflow. Kernel version/derived ISO are 0.5.0 after the gate.

### Validation

Final host/ELF: **25,541 / 1,707 checks**. The 0.4.0-stamped candidate passed
**10,257 QEMU checks in 19 VMs**; final 0.5.0 process, NX-off and kernel-PF
suites passed **3,115 in three VMs**. Total: **40,620, zero failures; 22 VMs reaped**.
Twelve usertests created/reaped 480 processes, contained 156 faults and completed
3,934,800 syscalls. `.text`/`.data`/`.limine_requests` match between phases;
`.rodata` differs by one version byte. Final host/ELF count once; the full
matrix was not repeated after stamping. [Evidence](docs/validation-astra-v0.5.json).

### Known limits

- BSP, one thread per process, no FPU/vector context or TLS; NX is required.
- Embedded probes only: no file-backed ELF loader, VFS, fork/exec/waitpid or POSIX.
- Syscalls keep IF=0, including bounded WRITE; no real-time latency guarantee.
- Kernel table/heap retention and HHDM alias limits persist; private process
  pages/tables are reclaimed. Manual/visual/UEFI/hardware acceptance is separate.

## v0.4.0 (unreleased)

GREEN local milestone on `astra-campaign`, recorded on 2026-09-14 at
`7e07719f2207fca29de4c7bafcaa296d2dd30b8a`. No public tag, merge or push.

### Added

- Kernel threads sharing CR3, bounded to 64 registry entries including shell/idle.
- Two-tick round-robin timer preemption, voluntary yield, deadline-checked sleep,
  input wakeup, nested preemption control and deferred zombie cleanup.
- Per-thread 64 KiB stacks with an unmapped lower guard page, PMM rollback,
  descriptor generations and deferred stack/TCB release.
- Synthetic supervisor bootstrap frames and GPR/CF/DF context-preservation probes.
- `ps`/`threads`, `schedulerstats`, `schedtest`, `sleep` and fatal `fault stack`.
- Host scheduler/stack models and a bounded headless scheduler regression harness.

### Changed

- Interrupt dispatch returns the selected 176-byte frame in RAX; Assembly adopts
  it before restoring all 15 GPRs and executing IRETQ. INT240 remains DPL0.
- Shell blocks for input while idle waits with STI/HLT; IRQ EOI precedes scheduling.
- Logging and direct terminal editing respect preemption; fatal output remains
  independent of scheduler integrity.
- Kernel version and derived ISO name are 0.4.0 after the candidate gate passed.

### Validation

Final clean build: **22,794 host and 1,563 ELF/ABI checks**, kernel and ISO passed.
The candidate matrix, still stamped 0.3.0, passed **6,081 QEMU checks in 15 VMs**.
After stamping 0.4.0, boot, scheduler and stack-fault checks passed **1,243 checks
in three VMs**. Total recorded: **31,681 assertions, zero failures; 18 VMs reaped**.

`.text`, `.data` and `.limine_requests` match across the stamp; `.rodata` differs
by one version byte. Host/ELF are counted once, QEMU phases separately. The full
RAM/NX matrix was run on the candidate, not repeated after stamping. All v0.3
host suites remain; new stack-fixture aggregation changes counting granularity
without removing scenarios or byte/page checks. See [the evidence](docs/validation-astra-v0.4.json)
and [scheduler contracts](docs/scheduler.md).

### Known limits

- BSP/ring 0 only; shared CR3/FS/GS and no FPU/SSE/AVX context switching.
- No user processes, priorities, SMP or real-time latency guarantee.
- Heap and some ownership transactions keep IF disabled; core validation is
  O(N²) within the fixed 64-thread bound.
- Bootstrap/IST stacks remain unguarded; the explicit guard fault is not a
  recursive stack-overflow test.
- Empty page tables and expanded heap pages remain retained; HHDM limits persist.
- Visual, physical keyboard, UEFI and physical hardware acceptance remain separate.

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
