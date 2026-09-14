# Layout de memória — UTAMO OS v0.2

O kernel ELF64 permanece ligado em `0xffffffff80000000`. Limine informa a
base física real e o offset HHDM por requests explícitos; nenhuma base física
ou identidade físico/virtual é presumida. O VMM mantém o CR3 e a árvore de
quatro níveis do boot, validando e estendendo essa árvore em região própria.
O contrato completo está em [memory-management.md](memory-management.md).

## Imagem e stacks

| Região | Alinhamento | ELF | Mapping principal após proteção, com NX |
| --- | --- | --- | --- |
| `.limine_requests` | Requests em 8 bytes; segmento em página | RW | RW/NX |
| `.text` | 4096 bytes | RX | RX |
| `.rodata` | 4096 bytes | R | RO/NX |
| `.data` e `.bss` | Início em página | RW | RW/NX |
| `.debug_*` | Fora dos segmentos carregados | Não ALLOC | Somente arquivo ELF no host |

O linker exporta limites de kernel, text, rodata, data e BSS para inspeção e
proteção. `PT_GNU_STACK` não é executável. A aplicação de permissões é
registrada como aplicada ou adiada; não presume poder dividir huge pages herdadas.

Stack bootstrap de 64 KiB, três stacks IST de 16 KiB, GDT/TSS e IDT continuam
na imagem estática do kernel. O build usa `-mno-red-zone` e frame pointers.
A região física KERNEL_AND_MODULES não é distribuída pelo PMM.
Não há guard pages ou heap em v0.2.

## HHDM e mapa físico

O mapa próprio conserva até 512 regiões, com intervalos semiabertos, validação
de overflow, ordenação e reservas. Tipos desconhecidos do protocolo continuam
reservados. Somente regiões USABLE fornecem frames livres.
BOOTLOADER_RECLAIMABLE e ACPI RECLAIMABLE continuam indisponíveis para alocação.

Conversões HHDM aceitam apenas USABLE, BOOTLOADER_RECLAIMABLE,
KERNEL_AND_MODULES e FRAMEBUFFER, segundo o contrato da base revision 3.
Exigem range inteiro dentro de uma região e endereços canônicos. O VMM
verifica os mappings reais antes de inicializar os bitmaps.
Page tables herdadas devem ser BOOTLOADER_RECLAIMABLE, evitando sobreposição
com a primeira escrita de metadados do PMM.

O framebuffer permanece acessado por seu ponteiro virtual, pitch e máscaras
RGB originais; o VMM confere a correspondência física do range completo.
Não cria outro alias com cache policy diferente. Os limites de 8192 pixels
por eixo e 256 MiB, incluindo padding, continuam em `framebuffer.h`.

## Metadados e arena dinâmica

| Região | Contrato |
| --- | --- |
| Página física zero | Reservada; nunca retornada pelo PMM |
| Bitmaps PMM | Dois bits por frame do span; storage alinhado, escolhido em USABLE e reservado |
| Tabelas herdadas | Conservadas, sem reclaim do bootloader |
| Novas page tables | Frames PMM zerados, publicados e fixados permanentemente |
| `[0xffffc00000000000, 0xffffc08000000000)` | Slot PML4 384, 512 GiB para mutações públicas |
| `[0xffffc00000000000, 0xffffc00000400000)` | Primeiros 4 MiB dedicados aos selftests |

A arena começa vazia e deve ser disjunta de HHDM/kernel.
Só recebe mappings de 4 KiB sobre RAM USABLE alocada pelo PMM.
A consulta aceita outros endereços canônicos, inclusive folhas herdadas de
2 MiB/1 GiB. As operações públicas não alteram mappings do bootloader nem
fazem split dessas folhas.

Unmap remove a tradução; o chamador ainda deve liberar o frame de dados.
Tabelas intermediárias vazias permanecem fixadas e reutilizáveis.
Não há alocador de endereços virtuais, destruição de address space ou contagem
automática de aliases.

## Limite de proteção

O mapping principal usa RX/RO/NX/RW/NX conforme as seções quando compatível.
CR0.WP está habilitado e NX depende de CPUID/EFER.NXE.
Aliases HHDM herdados podem continuar graváveis e executáveis.
Logo, permissões dos segmentos e probes RO/NX não demonstram W^X global.

A etapa v0.3 introduzirá kernel heap; revisão dos aliases, guard pages,
recuperação de tabelas, processos e TLB shootdown exigem trabalho separado.

## v0.7 MMIO window

PCI devices use supervisor UC mappings in the 4 MiB window beginning at
0xffffc00080000000, beyond the existing heap/thread arenas. Ordinary RAM
map/protect/unmap operations reject this window. Physical RAM/HHDM types are
never admitted as device apertures. Mappings and table frames remain permanent.
See [storage](storage.md) for PAT validation and DMA ownership.
