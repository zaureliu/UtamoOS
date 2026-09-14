# Índice do projeto

Este índice organiza a árvore atual do UTAMO OS v0.2 por responsabilidade.
A arquitetura de memória está em [memory-management.md](memory-management.md).
O [relatório v0.1](v0.1-implementation-report.md) é um inventário histórico.
O tag v0.0.1 preserva os fontes e a documentação originais do baseline.

## Entrada pública

- [README](../README.md): visão geral, funcionalidades e primeiros comandos.
- [Contribuições](../CONTRIBUTING.md), [segurança](../SECURITY.md),
  [licença MIT](../LICENSE) e [changelog](../CHANGELOG.md).
- [Notas v0.1.0](releases/v0.1.0.md): escopo, validação e limitações.
- [Templates GitHub](../.github/): bugs, propostas e pull requests.

## Kernel

| Caminho | Responsabilidade |
| --- | --- |
| [kernel/arch/x86_64](../kernel/arch/x86_64/) | Entrada NASM, linker, adaptador Limine, CPU/I/O, serial, GDT/TSS, IDT/stubs e PIC |
| [kernel/core](../kernel/core/) | Inicialização, logging, panic e shell |
| [kernel/drivers/video](../kernel/drivers/video/) | Framebuffer, fonte e terminal |
| [kernel/drivers/timer](../kernel/drivers/timer/) | PIT e conversão de ticks |
| [kernel/drivers/input](../kernel/drivers/input/) | Controlador e teclado PS/2 |
| [kernel/input](../kernel/input/) | Buffer de scancodes e decoder set 1 |
| [kernel/interrupts](../kernel/interrupts/) | Dispatch de exceções/IRQs e diagnóstico fatal |
| [kernel/lib](../kernel/lib/) | Biblioteca freestanding, formatter e parser de linha |
| [kernel/memory](../kernel/memory/) | Mapa físico, helpers/HHDM, PMM, VMM e selftests |
| [kernel/include/utamo](../kernel/include/utamo/) | Interfaces e versão central do kernel |

Os diretórios [kernel/fs](../kernel/fs/), [kernel/net](../kernel/net/),
[kernel/scheduler](../kernel/scheduler/), [kernel/syscall](../kernel/syscall/),
[libc](../libc/) e [userspace](../userspace/) contêm notas de escopo futuro,
não implementações desses subsistemas.

## Build, dependências e testes

- [Makefile](../Makefile): kernel, host tests, inspeção e ISO.
- [limine.conf](../limine.conf): configuração de boot.
- [Scripts](../scripts/README.md): ISO, inspeção ELF/ABI e QEMU headless.
- [Suíte de memória QEMU](../scripts/test-memory-qemu.py): PMM/VMM, comandos, accounting e probes PF.
- [Testes](../tests/README.md): cobertura executada e limites da evidência.
- [Limine](../third_party/limine/README.md): versão fixada e proveniência.
- [Fonte bitmap](../assets/font/README.md): origem e licença do asset.

Artefatos de build, toolchain local, checkout vendor e logs brutos ficam
fora do versionamento, conforme [.gitignore](../.gitignore).

## Guias técnicos

- [Arquitetura](architecture.md) e [processo de boot](boot-process.md).
- [Interrupções e frame](interrupts.md), [GDT/IDT](architecture.md) e
  [teclado/shell](keyboard.md).
- [Layout de memória](memory-layout.md) e [gerenciamento PMM/VMM](memory-management.md).
- [Ambiente](development-environment.md), [coding style](coding-style.md) e
  [debugging](debugging.md).
- [Roadmap](roadmap.md) e [development log](development-log.md).
- [Relatório v0.2](v0.2-implementation-report.md) e
  [evidências v0.2](validation-v0.2.json).
- [Relatório histórico v0.1](v0.1-implementation-report.md) e
  [evidências v0.1](validation-v0.1.json).

[Relatório de geração](generation-report.md),
[auditoria estática inicial](static-audit.md) e
[roteiro inicial de boot](../tests/boot-validation.md) são registros históricos
da preparação v0.0.1; suas pendências não representam o estado atual.
