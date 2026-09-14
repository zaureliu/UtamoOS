# Changelog

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
