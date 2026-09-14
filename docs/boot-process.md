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
9. Emite a mensagem final e entra em `cpu_halt`, que desabilita interrupções e
   repete `hlt` indefinidamente, inclusive se houver retorno de uma SMI.

## Dependências do handoff

O kernel depende do ambiente x86_64 entregue pelo Limine, inclusive mappings
iniciais e carregamento de segmentos ELF. Respostas de boot têm ponteiros virtuais;
o endereço do framebuffer é usado diretamente, sem somar HHDM novamente.
O memory map contém bases físicas, que não são dereferenciadas.
[Contrato de handoff e memória](https://github.com/limine-bootloader/limine/blob/v8.7.0/PROTOCOL.md).

A `.bss` usa `SHT_NOBITS`/`PT_LOAD` e depende do zero-fill do loader ELF. A pilha
própria remove a dependência da pilha do bootloader após `_start`; GDT e page
tables iniciais ainda permanecem sob o contrato de boot. Não reutilize regiões
bootloader-reclaimable nesta versão, mesmo após copiar o memory map.

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
| Exceção de CPU | Sem handler próprio; diagnóstico real requer GDB/QEMU |

`System halted safely.` indica apenas o término intencional do fluxo do kernel.
Não representa shutdown ACPI, sistema multitarefa saudável ou proteção contra NMI.

## Artefatos futuros

`make` produzirá o ELF com símbolos de debug; `make iso` preparará uma ISO híbrida
com boot BIOS e x86_64 UEFI. A sintaxe da configuração segue
[CONFIG.md v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/CONFIG.md).
A ISO e o boot ainda não foram produzidos/observados. Execute o roteiro em
[tests/boot-validation.md](../tests/boot-validation.md) no ambiente pessoal.
