# Layout e estratégia de memória

## Estado do milestone

O kernel é ELF64 estático, ligado no higher half em `0xffffffff80000000`.
O endereço físico de carga é escolhido pelo loader e não é presumido igual
ao virtual. A paginação inicial de quatro níveis permanece a do bootloader.
O kernel não modifica CR3 nem cria mappings novos em 0.0.1.

| Região do ELF | Alinhamento | Flags do segmento | Uso |
| --- | --- | --- | --- |
| `.limine_requests` | 8 bytes, início em página | RW | Tags e respostas do loader |
| `.text` | Página de 4096 bytes | RX | Código C/NASM |
| `.rodata` | Página de 4096 bytes | R | Strings e bitmap da fonte |
| `.data` e `.bss` | Segmento em página; BSS 16 bytes | RW | Estado estático e pilha |
| `.debug_*` | Endereço não carregado | Sem PT_LOAD | GDB no host |

`__kernel_start`, `__kernel_end`, `__bss_start` e `__bss_end` delimitam o ELF.
O script define `PT_GNU_STACK` sem execução. Isto descreve permissões dos
segmentos; não promete W^X global, pois aliases herdados do HHDM ainda existem.
Uma política de permissões completa depende do VMM próprio e da revisão dos aliases.

A pilha bootstrap tem 65536 bytes em `.bss`; RSP começa no topo e cresce para
endereços menores. Não há guard page, stack canary ou detector de overflow.
O build usa `-mno-red-zone` e frame pointers. O mapa, terminal e framebuffer
ficam em estado estático para evitar grandes temporários na pilha.

## Mapa físico

`struct memory_region` conserva base física, comprimento e tipo interno.
`struct memory_map` possui até 512 entradas, contagem e totais utilizável e
bootloader-reclaimable em bytes. `boot_read_memory_map` copia valores e não
armazena ponteiros Limine dentro do mapa próprio.

A importação rejeita contagem excessiva, ponteiros nulos, intervalos com overflow,
ordem decrescente, alinhamento inválido de usable/reclaimable e sobreposição
envolvendo essas regiões. Entradas de comprimento zero são ignoradas pelo
adaptador. Sobreposições entre regiões reservadas são toleradas; tipos futuros
desconhecidos são tratados como reservados. Falha descarta a contagem e os totais,
sem disponibilizar um mapa parcial ao fluxo normal.

O modelo reconhece usable, reserved, ACPI reclaimable, ACPI NVS, bad memory,
bootloader reclaimable, kernel/modules e framebuffer. Somente `usable_bytes`
entra no número `Total usable memory`, dividido por 1048576. Não se somam todas
as regiões para obter RAM instalada: regiões reservadas podem representar MMIO
e sobrepor outras descrições.

Invariantes e interpretação foram confrontadas com a
[seção Memory Map do protocolo selecionado](https://github.com/limine-bootloader/limine/blob/v8.7.0/PROTOCOL.md#memory-map-feature).
Checks de nulidade/aritmética não comprovam que um ponteiro arbitrário é válido:
o bootloader continua parte da base de confiança.

## Framebuffer

A abstração armazena endereço virtual, pitch em bytes, dimensões e máscaras RGB.
Escreve bytes `volatile`, respeitando pitch e formato; não pressupõe que toda
linha tenha exatamente `width * 4` bytes ou que RGB seja sempre BGRX.
O terminal só escreve pixels, sem ler MMIO para scroll.

O adaptador seleciona até 64 descritores informados no boot. A implementação
de framebuffer limita geometria/extensão, valida multiplicações e soma do
endereço final antes de permitir desenho. Limites exatos estão em `framebuffer.h`.
Essas restrições são deliberadas para manter o bootstrap limitado: 8192 pixels
por eixo e 256 MiB incluindo padding. A validade do mapeamento continua sendo
responsabilidade do chamador; não é verificada por esses limites aritméticos.

## Próximas etapas

1. Instalar GDT/IDT/TSS/IST próprios e diagnóstico de exceções.
2. Obter explicitamente HHDM e endereço físico/virtual do kernel via requests
   específicos, copiar os metadados necessários e registrar todas as reservas.
3. Criar bitmap do PMM em memória previamente reservada. Começar com tudo ocupado,
   liberar apenas páginas inteiras de regiões usable e reservar o frame zero
   por política própria, kernel, módulos, framebuffer e metadados do allocator.
4. Manter as páginas do bootloader, GDT herdada e page tables ocupadas até
   substituir todas as dependências. ACPI reclaimable tem ciclo de vida separado.
5. Construir VMM com permissões por mapping, checagem de endereços e guard pages.
   Definir ownership e contabilização antes de `kmalloc`/`kfree`.
6. Introduzir espaços virtuais por processo, cópia user/kernel e TLB shootdown
   antes de SMP e isolamento de processos.

As etapas acima são projeto futuro. Nenhuma página é alocada, liberada ou
reivindicada por um PMM nesta versão.
