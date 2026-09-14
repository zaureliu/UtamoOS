# UTAMO OS v0.0.1 Generation Report

> Registro histórico de 2026-09-13, anterior ao primeiro boot validado.
> As limitações e pendências abaixo descrevem aquele momento. O estado atual
> está no [relatório v0.1](v0.1-implementation-report.md). Caminhos pessoais
> foram normalizados para `UtamoOS/` na preparação pública.

Entrega: 2026-09-13. Diretório confirmado antes da primeira escrita:
`UtamoOS/`.

**Milestone concluído do ponto de vista de código-fonte. Preparado para
validação; compilação, testes, geração de ISO e boot ainda pendentes.**

## Arquivos entregues

O projeto contém 66 arquivos: 11 unidades C do kernel, dois programas C de
testes de host, três fontes NASM, 14 headers próprios, um header Limine adaptado,
linker script, Makefile, configuração de boot, script de ISO, licença,
configurações de texto/Git e documentação. O
[índice atual](file-index.md) organiza os principais caminhos; o tag
v0.0.1 preserva o inventário original dessa entrega.

Entradas principais:

- [README](../README.md), [licença MIT](../LICENSE) e [changelog](../CHANGELOG.md).
- [kernel_main](../kernel/core/main.c), [entrada NASM](../kernel/arch/x86_64/entry.asm)
  e [linker](../kernel/arch/x86_64/linker.ld).
- [Integração Limine](../kernel/arch/x86_64/boot.c) e
  [proveniência do header](../third_party/limine/README.md).
- [Makefile](../Makefile), [script da ISO](../scripts/make-iso.sh) e
  [configuração Limine](../limine.conf).
- [Testes portáveis](../tests/README.md), [testes de vídeo](../tests/video-tests.md)
  e [matriz de boot](../tests/boot-validation.md).

## Arquitetura e componentes implementados

Kernel próprio x86_64, ELF64 estático, C17 freestanding e NASM, ABI SysV AMD64,
target `x86_64-elf`, endereço virtual inicial `0xffffffff80000000`, pilha própria
de 64 KiB e paginação inicial de quatro níveis fornecida pelo Limine.

Fluxo: firmware BIOS/UEFI → Limine → `_start` → `kernel_main` → serial →
validação do protocolo → framebuffer/terminal → logs/mapa físico → halt.

Estão implementados framebuffer RGB 24/32 bpp, pixels/cores/limpeza, terminal
ASCII com fonte original MIT, cursor lógico e newline/wrap, logging por sinks,
COM1 com polling limitado, panic com arquivo/linha, utilitários CPU/I/O,
cópia/validação do mapa de memória e total utilizável, primitivas de memória e
strings, formatter e preparação de ISO BIOS/UEFI.

O formatter suporta `%s`, `%c`, `%d`, `%u`, `%x`, `%p`, `%%`, `%lld`, `%llu`
e `%llx`, com saídas por callback ou buffer limitado. O logger já envia para
serial e terminal simultaneamente quando disponíveis.

## Decisões principais

- Limine v8.7.0, base revision 3, API revision 2; bootloader futuro fixado em
  v8.7.0-binary e commit registrado. O header local é um subset identificado.
- Nenhum terminal pronto de outro sistema, heap prematuro ou dependência da libc
  do host no kernel. `memmove` também atende necessidades do GCC freestanding.
- Um BSP e interrupções mascaráveis desabilitadas durante todo o fluxo.
- Mapa físico com 512 entradas e sem liberação de memória do bootloader.
- Framebuffer com pitch/máscaras validados e limites explícitos de bootstrap.
- Default de Make constrói somente o kernel; dependências não são baixadas por targets.

Justificativas e alternativas estão em [development-log.md](development-log.md).

## Componentes não implementados

GDT/IDT/TSS próprios, handlers de exceção, IRQs, timers, teclado, ACPI/APIC,
SMP, PMM, bitmap allocator, VMM próprio, heap, threads, scheduler, ring 3,
processos, ELF loader de userspace, syscalls, `init`, shell, libc de userspace,
initramfs/RAMFS/VFS/FAT32, PCI/PCIe, AHCI/NVMe, networking e GUI.

Esses subsistemas têm reservas documentadas e milestones no
[roadmap](roadmap.md); não há stubs que afirmem implementá-los.

## Revisões e limites da evidência

Duas revisões estáticas completas foram realizadas, com revisão independente
das interfaces e releitura das correções. O
[relatório de auditoria](static-audit.md) registra escopo, correções e riscos.
Não foi identificado bloqueador adicional por leitura após os ajustes.

Nenhum compilador, assembler, linker, Make, script do projeto, teste, QEMU ou
GDB foi executado. Nenhuma dependência foi instalada. Não houve alteração de
PATH, registro, configurações Windows, privilégios administrativos, Git init,
push ou publicação. Nenhum arquivo de projeto foi escrito fora de `UtamoOS/`.

## Preparação e comandos posteriores

No computador pessoal, use Linux nativo ou WSL2 Ubuntu, GCC/binutils para host,
cross GCC/binutils `x86_64-elf`, NASM, GNU Make, Bash/utilitários Unix, Git,
xorriso, Limine selecionado, QEMU e GDB. OVMF é usado na validação UEFI.
O guia de [ambiente](development-environment.md) contém dependências e instruções
para construir uma toolchain dentro do projeto sem modificar PATH.

Após preparar a toolchain local e o checkout Limine, execute na raiz, em casa:

```sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" run
```

Para depurar, substitua `run` por `debug` e, em outro terminal:

```sh
gdb build/utamo-kernel.elf
```

Dentro do GDB:

```gdb
target remote 127.0.0.1:1234
hbreak kernel_main
continue
```

## Riscos e primeira tarefa recomendada

A primeira tarefa em casa é ler o guia de ambiente, registrar versões da
toolchain e executar `make test-host`. Depois compile e inspecione o ELF antes
da primeira sessão QEMU/GDB. Siga a matriz BIOS/UEFI e registre a evidência real.

Precisam de validação: warnings/linker na toolchain escolhida, geração da ISO,
ABI de entrada, respostas Limine, legibilidade da fonte, acesso ao framebuffer,
COM1 e halt. Sem IDT própria, exceções/NMI podem impedir o panic e causar triple
fault. Sem serial/framebuffer disponíveis, a parada antecipada pode ser silenciosa.
O logger ainda não é concorrente; terminal limpa a tela no transbordo; não há
shutdown ACPI nem promessa de execução em hardware físico nesta entrega.
