# Changelog

## UTAMO OS 0.1.0 — 2026-09-14

Estado: implementado, compilado e testado em host; boot e interrupções
validados em QEMU headless. Input real e framebuffer visual pendentes de
validação manual; tag de release reservado ao usuário.

### Added

- GDT própria, TSS64 e IST para Double Fault/NMI/Machine Check.
- IDT completa e stubs NASM 64-bit, frame de 176 bytes e ABI SysV/IRETQ.
- Diagnóstico serial/framebuffer, PF/CR2 e probes UD2/DIV0/PF.
- PIC8259 com máscaras, cascade, EOI e IRQ7/15 espúrias.
- PIT nominal 100 Hz, ticks monotônicos e idle STI/HLT.
- PS/2 set 1, buffer circular e decoder independente.
- Shell help/clear/version/sysinfo/mem/uptime/echo/halt/fault.
- Testes host, inspeção ELF/ABI e automação QEMU headless limitada.

### Changed

- Evolução do baseline v0.0.1, com interfaces existentes preservadas.
- Boot normal chega a utamo>; halt permanente somente explícito/fatal.
- Versão 0.1.0 centralizada no header; nome ISO derivado.
- Flag NASM deduplicada; -Werror e demais warnings mantidos.
- Arquitetura, debugging e evidências documentados.

### Validation

3044 checks originais preservados. Final: 5214 host, 1303 ELF e 126 QEMU,
todos aprovados. Build limpo e ISO com toolchain existente.
Sem GUI, push ou tag v0.1.0. Aceite integral condicionado aos itens manuais
do [relatório v0.1](docs/v0.1-implementation-report.md).


## UTAMO OS 0.0.1 — 2026-09-13

Estado: implementado em código-fonte; teste pendente no ambiente de desenvolvimento.

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
