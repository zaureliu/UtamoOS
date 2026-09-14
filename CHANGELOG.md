# Changelog

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
