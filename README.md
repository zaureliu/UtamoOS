# UTAMO OS 0.1.0

Sistema operacional experimental x86_64 com kernel próprio, C17 freestanding
e NASM. Esta versão evolui o baseline **v0.0.1 validado em boot real**, preservado
pelo tag Git. Linux/WSL é somente o ambiente de desenvolvimento; Limine v8.7.0
carrega o ELF64 próprio em higher half.

**Estado:** implementação compilada, testes host aprovados, prompt e
interrupções validados por QEMU headless/serial. Entrada PS/2 real e aparência
do framebuffer permanecem **PENDENTES DE VALIDAÇÃO MANUAL**. O aceite integral
de v0.1 ainda depende dessas verificações. Consulte o
[relatório com evidências](docs/v0.1-implementation-report.md).

## O que existe

- Boot Limine, stack64 KiB, framebuffer/terminal bitmap, COM1, logger, panic,
  memory map e biblioteca própria do baseline.
- GDT própria, TSS e três stacks IST; IDT completa com256 gates e stubs64-bit.
- Exceções fatais com GPRs/RIP/RSP/flags, serial primeiro e framebuffer depois.
  Page Fault exibe CR2 e bits P/W/U/RSVD/I.
- PIC8259 remapeado, EOI e tratamento de IRQ7/15 espúrias.
- PIT nominal100 Hz, ticks monotônicos e loop ocioso com STI/HLT.
- Driver PS/2 set1, fila de scancodes e decoder fora da ISR.
- Shell de kernel `utamo>`: `help clear version sysinfo mem uptime echo halt fault`.
  `fault ud2|div0|pf` é explícito e fatal, nunca executado no boot normal.

Não há PMM, VMM próprio, heap, scheduler, ring3, filesystem, rede ou reboot.
A shell não cria processos. `mem` mostra metadados do mapa recebido no boot,
não memória livre de um allocator. Limites de input em [keyboard.md](docs/keyboard.md).

## Build com o ambiente existente

Execute em `~/UtamoOS` no WSL2 Ubuntu, branch `v0.1-dev`.
O compilador do kernel é exclusivamente o cross `x86_64-elf`; os testes host
usam o compilador nativo. Não há downloads nem instalações automáticas.

```sh
make clean
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
python3 scripts/test-qemu.py --marker 'utamo> ' --name boot-local \
    --check-gdt --check-idt --check-timer
```

Artefatos: `build/utamo-kernel.elf`, `build/utamo-os-0.1.0.iso` e
`build/validation/<nome>/`. O inspetor verifica ELF, segmentos, símbolos,
tabela de stubs e convenção ABI. A versão do kernel/nome ISO vem de
`kernel/include/utamo/version.h`; documentação de release registra a versão
explicitamente. O menu Limine usa o nome sem duplicar a versão.

**QEMU automatizado é sempre headless**, com `-display none`, serial, nenhuma
janela GTK/SDL e uma VM por vez. O harness limita duração, recusa instâncias
concorrentes e encerra/recolhe somente seu próprio processo em sucesso ou falha.
O padrão dos targets QEMU também é headless; nesta sessão não se executaram
`make run` nem `make run-uefi`. Serial fornece saída; digitação da shell usa
PS/2 e não recebe caracteres digitados na entrada serial do host.

## Evidência e documentação

O baseline inicialmente passou3044 checks host. A evolução mantém todos esses
testes e acrescenta testes de descritores, diagnóstico, PIC/PIT, input e shell.
Quantidades finais, hashes, commits, falhas corrigidas e limites de cada
validação estão no [Implementation Report](docs/v0.1-implementation-report.md).

- [Arquitetura](docs/architecture.md), [interrupções](docs/interrupts.md),
  [teclado/shell](docs/keyboard.md).
- [Debugging headless](docs/debugging.md), [testes](tests/README.md),
  [scripts](scripts/README.md).
- [Development log](docs/development-log.md), [roadmap](docs/roadmap.md),
  [changelog](CHANGELOG.md), [padrão de código](docs/coding-style.md).
- Os relatórios de geração/auditoria de0.0.1 são documentos históricos; suas
  declarações de testes pendentes referem-se à geração inicial, anterior ao
  boot validado e ao desenvolvimento atual.

O tag `v0.0.1` não é alterado. Nenhum push é feito e o tag `v0.1.0` fica a cargo
do usuário após validação manual. Código, documentação e fonte bitmap próprios
sob MIT; proveniência Limine em [third_party/limine](third_party/limine/README.md).
