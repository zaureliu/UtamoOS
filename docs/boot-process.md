# Processo de boot

## Versão do contrato

UTAMO seleciona **Limine v8.7.0**, **base revision 3**, e nomes do header com
**API revision 2**. São números diferentes: release do bootloader, contrato
global de handoff e versão da API C. Requests de framebuffer e memory map usam
revision 0; paginação usa revision 1 para fixar mínimo/máximo em quatro níveis.
O retorno só é aceito após a base revision ser confirmada.

As definições usadas foram conferidas no
[header oficial v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/limine.h).
O arquivo local é um subconjunto marcado, com licença original preservada.
Não se usa o antigo terminal do Limine.

## Caminho implementado

1. Firmware e Limine carregam `boot/utamo-kernel.elf` indicado em `limine.conf`.
2. O linker preserva delimitadores, base revision e requests em segmento gravável,
   alinhados em oito bytes. Isso permite que o bootloader preencha respostas.
3. `_start` executa `cli`/`cld`, seleciona pilha própria de 64 KiB, alinha RSP
   em 16 bytes antes de `call kernel_main` e zera RBP como sentinela de backtrace.
4. `kernel_main` inicializa COM1 e conecta o sink serial se disponível.
5. O adaptador verifica base revision e resposta de paginação. Nada usa respostas
   de framebuffer/memória antes desses checks.
6. Seleciona o primeiro framebuffer RGB compatível, validando formato e geometria.
7. Inicializa terminal, limpa a tela, conecta sink e emite identificação/estado.
8. Copia o mapa físico para estruturas próprias e soma somente regiões utilizáveis.
9. Instala GDT própria, TSS e três pilhas IST, depois conecta o terminal de
   emergência e carrega os 256 gates da IDT.
10. Remapeia o PIC com IRQs mascaradas; configura PIT em 100 Hz e teclado PS/2.
11. Desmascara somente IRQ0 e IRQ1 após os drivers estarem prontos e habilita IF.
12. Emite `UTAMO OS ready.`, inicializa o shell e mostra `utamo> `.
13. Processa input no fluxo principal. Antes de repousar, desabilita IF, confere
    novamente a fila e executa `sti; hlt` contíguos se ela estiver vazia.
    Interrupções despertam a CPU; o loop continua enquanto não ocorrer halt
    explícito ou falha fatal.

O fluxo implementado está em [kernel_main](../kernel/core/main.c).
[Interrupções](interrupts.md) e [teclado/shell](keyboard.md) detalham os contratos.

## Dependências do handoff

O kernel depende do ambiente x86_64 entregue pelo Limine, inclusive mappings
iniciais e carregamento de segmentos ELF. Respostas de boot têm ponteiros virtuais;
o endereço do framebuffer é usado diretamente, sem somar HHDM novamente.
O memory map contém bases físicas, que não são dereferenciadas.
[Contrato de handoff e memória](https://github.com/limine-bootloader/limine/blob/v8.7.0/PROTOCOL.md).

A `.bss` usa `SHT_NOBITS`/`PT_LOAD` e depende do zero-fill do loader ELF. A pilha
própria remove a dependência da pilha do bootloader após `_start`. A GDT
inicial é substituída por `gdt_init`; as page tables continuam sendo as do
bootloader. Não reutilize regiões bootloader-reclaimable nesta versão, mesmo
após copiar o memory map e instalar os descritores próprios.

Não se pede SMP, ACPI, módulos, kernel-address ou HHDM sem necessidade de uso.
Esses requests serão acrescentados junto ao subsistema que souber validar e
administrar sua vida útil. Não há acesso físico improvisado por identity mapping.

## Falhas observáveis

| Condição | Comportamento implementado |
| --- | --- |
| COM1 ausente ou timeout | Continua; avisa no terminal quando disponível |
| Revisão base ou paging incompatível | Panic via serial se disponível; halt |
| Framebuffer ausente/incompatível | Panic via serial se disponível; halt |
| Falha de terminal/registro de sink | Panic pelos sinks já registrados; halt |
| Mapa ausente, inválido ou acima de 512 entradas | Panic em framebuffer e serial disponíveis |
| Falha ao inicializar IDT ou teclado PS/2 | Panic pelos sinks disponíveis; IF continua desabilitado |
| Exceção de CPU após IDT | Dump fatal com frame normalizado, serial antes do terminal; CLI/HLT |
| Page fault após IDT | Mesmo dump, incluindo CR2 e bits relevantes do error code |
| Comando `halt` | Mensagem explícita e `cpu_halt` permanente com IF=0 |

Antes da instalação da IDT, falhas de CPU ainda dependem do ambiente de
handoff e podem exigir GDB/QEMU. IST não substitui guard pages nem torna
qualquer falha recuperável. `cli` não bloqueia NMI ou machine checks.

`System halted safely.` pertence ao fluxo histórico v0.0.1. O estado
operacional v0.1.0 é o shell com IRQs ativas. `halt` não é shutdown ACPI.

## Artefatos e evidência

`make` gera `build/utamo-kernel.elf` com símbolos de debug; `make iso` gera
`build/utamo-os-0.1.0.iso`, contendo assets BIOS e x86_64 UEFI. A sintaxe da
configuração segue
[CONFIG.md v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/CONFIG.md).
A geração da ISO e o boot BIOS foram observados em QEMU headless; suporte de
composição UEFI não equivale a uma execução UEFI validada.

O [relatório v0.1](v0.1-implementation-report.md) registra os resultados.
Para repetir os testes atuais, consulte [debugging](debugging.md) e
[testes](../tests/README.md). O [roteiro inicial](../tests/boot-validation.md)
é preservado como histórico do baseline.
