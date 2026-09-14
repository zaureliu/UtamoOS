# UTAMO OS

UTAMO OS é um sistema operacional educacional e experimental para x86_64,
com kernel próprio, chamado **utamo-kernel**. Esta entrega contém os fontes
do milestone **0.0.1**, scripts de build e documentação.

**Estado: implementado em código-fonte, preparado para validação. Compilação,
linkedição, testes de host e boot estão pendentes no ambiente pessoal de
desenvolvimento.** A geração incluiu duas revisões estáticas, documentadas
em [docs/static-audit.md](docs/static-audit.md); elas não substituem execução.

Este projeto não é uma distribuição Linux, kernel Linux modificado, BusyBox,
wrapper ou programa que simula um kernel. Limine é o bootloader externo que
carrega o ELF do kernel próprio. Linux/WSL é somente o ambiente futuro de build.

## Escopo desta versão

O caminho implementado é firmware → Limine → ELF64 → `_start` →
`kernel_main()` → framebuffer/terminal → informações de boot → `cli; hlt`.
O processador principal permanece parado ao final; isso não desliga a máquina.

| Componente | Estado no fonte |
| --- | --- |
| Entrada x86_64, pilha própria e linker ELF64 | Implementado |
| Integração Limine v8.7.0, base revision 3, API revision 2 | Implementado |
| Framebuffer RGB 24/32 bpp e pixels/cores/limpeza | Implementado |
| Terminal próprio, fonte bitmap ASCII, cursor lógico | Implementado |
| Logger com múltiplos destinos e níveis | Implementado |
| COM1 115200 8N1 com polling limitado | Implementado |
| Panic com mensagem, arquivo, linha e parada | Implementado |
| Cópia/validação do memory map e total utilizável | Implementado |
| Biblioteca freestanding e formatter pequeno | Implementado |
| Testes de host e roteiro BIOS/UEFI/GDB | Preparados; execução pendente |
| PMM, heap, interrupções, processos, arquivos e rede | Planejados; não implementados |

Não existe shell nesta versão. Os nomes reservados são `utamo>` para o prompt
futuro e `init` para o primeiro processo de userspace. O primeiro filesystem
planejado é RAM filesystem com importação de initramfs; FAT32 virá depois.

## Arquitetura

- C17 freestanding; NASM apenas para entrada, CPU e I/O de portas.
- Target GCC/binutils `x86_64-elf`; ABI SysV AMD64, ELF64 estático no higher half.
- Endereço virtual inicial `0xffffffff80000000`; paginação inicial de quatro níveis.
- Pilha bootstrap própria de 64 KiB, sem heap, sem red zone, sem SIMD/FPU.
- Interrupções mascaráveis desabilitadas; somente BSP; nenhuma recuperação de exceções ainda.
- Interfaces de framebuffer, terminal e mapa independentes das estruturas do Limine.
- MIT para código/documentação/fonte bitmap próprios; avisos BSD-0-Clause preservados
  no header adaptado do Limine. Veja [third_party/limine/README.md](third_party/limine/README.md).

O header do protocolo é um subconjunto explícito das declarações oficiais de
v8.7.0, e não uma API inventada nem uma cópia integral sem alterações.
O bootloader binário deverá ser obtido no computador pessoal; não está incluído.

## Organização

```text
UtamoOS/
  kernel/
    include/utamo/       contratos internos
    core/                kernel_main, log, panic
    arch/x86_64/         boot Limine, NASM, COM1, linker
    drivers/video/      framebuffer, terminal, fonte
    memory/              mapa físico validado
    lib/                 memória, strings, formatter
    interrupts/ fs/ scheduler/ syscall/ net/  reservas documentadas
  userspace/ libc/       reservas documentadas, fora do build
  third_party/limine/    header/proveniência; vendor futuro
  tests/                testes de host e critérios de aceitação
  scripts/              preparação explícita de ISO
  assets/font/          documentação da fonte incorporada
  docs/                 arquitetura, desenvolvimento e auditoria
  Makefile limine.conf LICENSE CHANGELOG.md
```

## Dependências e comandos futuros

**Execute os comandos desta seção somente no seu computador pessoal.**
Nenhum deles foi executado durante esta entrega no computador corporativo.

Use Linux nativo ou WSL2 Ubuntu. São necessários GCC e binutils para o host,
cross GCC/binutils `x86_64-elf`, NASM, GNU Make, Bash, utilitários Unix e Git.
A ISO requer xorriso e os arquivos do Limine **v8.7.0-binary**. QEMU e GDB
são necessários somente para execução/debugging; OVMF para a validação UEFI.
O kernel não usa a libc do host. O executável dos testes de host usa a libc
somente no harness de testes.

Siga primeiro [development-environment.md](docs/development-environment.md),
incluindo preparação do cross compiler e `third_party/limine/vendor`.
Depois, na raiz do projeto:

Os comandos simples abaixo pressupõem que `x86_64-elf-*` já esteja disponível
no ambiente pessoal. Se seguiu a construção local em `toolchain/prefix`,
acrescente `CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-"` a cada comando
de build do kernel, ISO, inspeção ou QEMU, conforme o guia; isso não modifica
PATH. `make test-host` usa somente o compilador nativo.

```sh
make test-host             # compila e executa apenas unidades portáveis
make                       # default: somente kernel
make inspect               # headers ELF, segmentos, símbolos indefinidos
make iso                   # build/utamo-os-0.0.1.iso
make run                   # QEMU TCG, BIOS, serial em stdio
```

Para depurar, use dois terminais:

```sh
# Terminal 1
make debug

# Terminal 2
gdb build/utamo-kernel.elf
# Dentro do GDB:
target remote 127.0.0.1:1234
hbreak kernel_main
continue
```

`make run-uefi` usa OVMF; seus caminhos são configuráveis. `make clean`
remove apenas `build/`. Nenhum target baixa dependências automaticamente.
Veja [debugging.md](docs/debugging.md) antes de interpretar um halt como erro.

## Saída esperada, ainda não observada

```text
UTAMO OS
Experimental x86_64 Operating System

Version: 0.0.1
Architecture: x86_64

[ OK    ] Limine boot protocol (base revision 3)
[ OK    ] Kernel loaded: utamo-kernel
[ OK    ] Framebuffer detected: ...
[ OK    ] Terminal initialized
...
Total usable memory: ... MiB

Welcome to UTAMO OS.

System halted safely.
```

As dimensões e a memória dependem do ambiente. O total é a soma das regiões
`USABLE` informadas no boot, arredondada para baixo em MiB; não é a RAM
instalada nem um contador de páginas livres de um allocator.

## Limites conhecidos e evolução

Falhas anteriores à inicialização do terminal dependem da serial para mostrar
mensagens. Sem serial e sem framebuffer válidos, o kernel para sem saída visível.
Sem IDT própria, uma exceção, NMI ou falha de mapeamento pode causar reset/triple
fault em vez de chegar a `kernel_panic`. O panic trata falhas explícitas do código;
não é um subsistema de captura de exceções.

O terminal limpa a tela ao transbordar, não faz scrollback e não interpreta ANSI
ou UTF-8. O logger ainda não é concorrente nem seguro para IRQs/SMP. O mapa
aceita até 512 entradas; framebuffers têm limites documentados. Nenhuma memória
do bootloader é liberada. Nenhum acesso a disco, rede ou firmware ACPI é feito.

Comece pela [validação de v0.0.1](tests/boot-validation.md), registre evidências
reais e somente depois avance para GDT/IDT/exceções. O
[roadmap](docs/roadmap.md) define a sequência até v1.0.

## Documentação

- [Arquitetura](docs/architecture.md) e [processo de boot](docs/boot-process.md).
- [Layout de memória](docs/memory-layout.md) e [padrão de código](docs/coding-style.md).
- [Ambiente](docs/development-environment.md), [debugging](docs/debugging.md) e [testes](tests/README.md).
- [Roadmap](docs/roadmap.md), [decisões](docs/development-log.md), [auditoria](docs/static-audit.md)
  e [changelog](CHANGELOG.md).
- [Relatório de geração](docs/generation-report.md) e [inventário completo](docs/file-index.md).

Versões seguem Semantic Versioning quando aplicável; durante `0.x` as APIs
internas podem mudar. `0.0.1` identifica este milestone de fontes, sem afirmar
uma release já validada em máquina. Não foi inicializado Git nem publicado nada.
